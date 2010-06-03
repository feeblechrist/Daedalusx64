/*
Copyright (C) 2006 StrmnNrmn

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "stdafx.h"
#include "RomSelectorComponent.h"
#include "UIContext.h"
#include "UIScreen.h"

#include <psptypes.h>
#include <pspkernel.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <pspgu.h>

#include "Math/Vector2.h"
#include "SysPSP/Graphics/DrawText.h"
#include "Graphics/ColourValue.h"
#include "Graphics/NativeTexture.h"

#include "Core/ROM.h"
#include "Core/RomSettings.h"

#include "../../Input/InputManager.h"
#include "../../Utility/Preferences.h"

#include "Utility/IO.h"
#include "Utility/ROMFile.h"

#include "SysPSP/Utility/PathsPSP.h"

#include "Math/MathUtil.h"

#include <string>
#include <vector>
#include <map>
#include <algorithm>

#include "../Utility/Thread.h"

/* Kernel Buttons functions */
extern "C" {
int getbuttons();
}

u32 new_kbuttons;

int romselmenuani = 0;
int romselmenufs = 31;
int romselmenudir = 0;
int showmoreinfo = 0;
bool sortbyletter = 0;
float romseltextoffset = 0.0f;
float romseltextrepos = 0.0f;
float romseltextscale = 0.0f;
bool isnextset = 0;
char catstr[85] = " #  a  b  c  d  e  f  g  h  i  j  k  l  m  n  o  p  q  r  s  t  u  v  w  x  y  z  ? ";

namespace
{
	const char * const		gRomsDirectories[] = 
	{
		"ms0:/n64/",
		DAEDALUS_PSP_PATH( "Roms/" ),
#ifndef DAEDALUS_PUBLIC_RELEASE
		// For ease of developing with multiple source trees, common folder for roms can be placed at host1: in usbhostfs
		"host1:/",
#endif
	};

	const char		gCategoryLetters[] = "#abcdefghijklmnopqrstuvwxyz?";

	enum ECategory
	{
		C_NUMBERS = 0,
		C_A, C_B, C_C, C_D, C_E, C_F, C_G, C_H, C_I, C_J, C_K, C_L, C_M,
		C_N, C_O, C_P, C_Q, C_R, C_S, C_T, C_U, C_V, C_W, C_X, C_Y, C_Z,
		C_UNK,
		NUM_CATEGORIES,
	};

	DAEDALUS_STATIC_ASSERT( ARRAYSIZE( gCategoryLetters ) == NUM_CATEGORIES +1 );

	ECategory		GetCategory( char c )
	{
		if( isalpha( c ) )
		{
			c = tolower( c );
			return ECategory( C_A + (c - 'a') );
		}
		else if( c >= '0' && c <= '9' )
		{
			return C_NUMBERS;
		}
		else
		{
			return C_UNK;
		}
	}

	char	GetCategoryLetter( ECategory category )
	{
		DAEDALUS_ASSERT( category >= 0 && category < NUM_CATEGORIES, "Invalid category" );
		return gCategoryLetters[ category ];
	}

	const u32				ICON_AREA_TOP = 48;
	u32				ICON_AREA_LEFT = 20;
	const u32				ICON_AREA_WIDTH = 256;
	const u32				ICON_AREA_HEIGHT = 177;

	const u32				TEXT_AREA_TOP = 70;
	u32				TEXT_AREA_LEFT = 300;
	const u32				TEXT_AREA_WIDTH = 148;
	const u32				TEXT_AREA_HEIGHT = 216;

	const char * const		gNoRomsText[] =
	{
		"Daedalus could not find any roms to load.",
		"You can add roms to the \\N64\\ directory on your memory stick,",
		"(e.g. P:\\N64\\)",
		"or the Roms directory within the Daedalus folder.",
		"(e.g. P:\\PSP\\GAME\\Daedalus\\Roms\\)",
		"Daedalus recognises a number of different filetypes,",
		"including .zip, .z64, .v64, .rom, .bin, .pal, .usa and .jap.",
	};

	const u32				CATEGORY_AREA_TOP = 255;
	const u32				CATEGORY_AREA_LEFT = 240;

	const char * const		gPreviewDirectory = DAEDALUS_PSP_PATH( "Resources/Preview/" );

	const f32				PREVIEW_SCROLL_WAIT = 0.65f;		// seconds to wait for scrolling to stop before loading preview (prevent thrashing)
	const f32				PREVIEW_FADE_TIME = 0.50f;		// seconds
}

//*************************************************************************************
//
//*************************************************************************************
struct SRomInfo
{
	CFixedString<100>		mFilename;

	RomID			mRomID;
	u32				mRomSize;
	ECicType		mCicType;

	RomSettings		mSettings;

	SRomInfo( const char * filename )
		:	mFilename( filename )
	{
		if ( ROM_GetRomDetailsByFilename( filename, &mRomID, &mRomSize, &mCicType ) )
		{
			if ( !CRomSettingsDB::Get()->GetSettings( mRomID, &mSettings ) )
			{
				// Create new entry, add
				mSettings.Reset();
				mSettings.Comment = "Unknown";

				//
				// We want to get the "internal" name for this rom from the header
				// Failing that, use the filename
				//
				std::string game_name;
				if ( !ROM_GetRomName( filename, game_name ) )
				{
					game_name = IO::Path::FindFileName( filename );
				}
				game_name = game_name.substr(0, 63);
				mSettings.GameName = game_name.c_str();
				CRomSettingsDB::Get()->SetSettings( mRomID, mSettings );
			}
		}
		else
		{
			mSettings.GameName = "Can't get rom info";
		}

	}
};

//*************************************************************************************
//
//*************************************************************************************
static ECategory Categorise( const char * name )
{
	char	c( name[ 0 ] );
	return GetCategory( c );
}

static bool SortByGameName( const SRomInfo * a, const SRomInfo * b )
{
	// Sort by the category first, then on the actual string.
	ECategory	cat_a( Categorise( a->mSettings.GameName.c_str() ) );
	ECategory	cat_b( Categorise( b->mSettings.GameName.c_str() ) );

	if( cat_a != cat_b )
	{
		return cat_a < cat_b;
	}

	return a->mSettings.GameName < b->mSettings.GameName;
}

//*************************************************************************************
//
//*************************************************************************************
class IRomSelectorComponent : public CRomSelectorComponent
{
		typedef std::vector<SRomInfo*>	RomInfoList;
		typedef std::map< ECategory, u32 >	AlphaMap;
	public:

		IRomSelectorComponent( CUIContext * p_context, CFunctor1< const char * > * on_rom_selected );
		~IRomSelectorComponent();

		// CUIComponent
		virtual void				Update( float elapsed_time, const v2 & stick, u32 old_buttons, u32 new_buttons );
		virtual void				Render();

	private:
				void				RenderPreview();
				void				RenderRomList();
				void				RenderCategoryList();

				void				AddRomDirectory(const char * p_roms_dir, RomInfoList & roms);

				ECategory			GetCurrentCategory() const;

				void				DrawInfoText( CUIContext * p_context, s32 y, const char * field_txt, const char * value_txt );

	private:
		CFunctor1< const char * > *	OnRomSelected; 
		RomInfoList					mRomsList;
		AlphaMap					mRomCategoryMap;
		u32							mCurrentSelection;
		s32							mCurrentScrollOffset;
		float						mSelectionAccumulator;
		std::string					mSelectedRom;

		bool						mDisplayFilenames;

		CRefPtr<CNativeTexture>		mpPreviewTexture;
		u32							mPreviewIdx;
		float						mPreviewLoadedTime;		// How long the preview has been loaded (so we can fade in)
		float						mTimeSinceScroll;		// 

		bool						mQuitTriggered;
};

//*************************************************************************************
//
//*************************************************************************************
CRomSelectorComponent::CRomSelectorComponent( CUIContext * p_context )
:	CUIComponent( p_context )
{
}

//*************************************************************************************
//
//*************************************************************************************
CRomSelectorComponent::~CRomSelectorComponent()
{
}

//*************************************************************************************
//
//*************************************************************************************
CRomSelectorComponent *	CRomSelectorComponent::Create( CUIContext * p_context, CFunctor1< const char * > * on_rom_selected )
{
	return new IRomSelectorComponent( p_context, on_rom_selected );
}

//*************************************************************************************
//
//*************************************************************************************
IRomSelectorComponent::IRomSelectorComponent( CUIContext * p_context, CFunctor1< const char * > * on_rom_selected )
:	CRomSelectorComponent( p_context )
,	OnRomSelected( on_rom_selected )
,	mCurrentSelection( 0 )
,	mCurrentScrollOffset( 0 )
,	mSelectionAccumulator( 0 )
,	mpPreviewTexture( NULL )
,	mPreviewIdx( u32(-1) )
,	mPreviewLoadedTime( 0.0f )
,	mTimeSinceScroll( 0.0f )
,	mQuitTriggered(false)
{
	for( u32 i = 0; i < ARRAYSIZE( gRomsDirectories ); ++i )
	{
		AddRomDirectory( gRomsDirectories[ i ], mRomsList );
	}

	stable_sort( mRomsList.begin(), mRomsList.end(), SortByGameName );

	// Build up a map of the first location for each initial letter
	for( u32 i = 0; i < mRomsList.size(); ++i )
	{
		const char *	p_gamename( mRomsList[ i ]->mSettings.GameName.c_str() );
		ECategory		category( Categorise( p_gamename ) );

		if( mRomCategoryMap.find( category ) == mRomCategoryMap.end() )
		{
			mRomCategoryMap[ category ] = i;
		}
	}
}

//*************************************************************************************
//
//*************************************************************************************
IRomSelectorComponent::~IRomSelectorComponent()
{
	for(RomInfoList::iterator it = mRomsList.begin(); it != mRomsList.end(); ++it)
	{
		SRomInfo *	p_rominfo( *it );

		delete p_rominfo;
	}
	mRomsList.clear();

	delete OnRomSelected;
}

//*************************************************************************************
//
//*************************************************************************************
void	IRomSelectorComponent::AddRomDirectory(const char * p_roms_dir, RomInfoList & roms)
{
	std::string			full_path;

	IO::FindHandleT		find_handle;
	IO::FindDataT		find_data;
	if(IO::FindFileOpen( p_roms_dir, &find_handle, find_data ))
	{
		do
		{
			const char * rom_filename( find_data.Name );
			if(IsRomfilename( rom_filename ))
			{
				full_path = p_roms_dir;
				full_path += rom_filename;

				SRomInfo *	p_rom_info = new SRomInfo( full_path.c_str() );

				roms.push_back( p_rom_info );
			}
		}
		while(IO::FindFileNext( find_handle, find_data ));

		IO::FindFileClose( find_handle );
	}
}

//*************************************************************************************
//
//*************************************************************************************
ECategory	IRomSelectorComponent::GetCurrentCategory() const
{
	if( !mRomsList.empty() )
	{
		return Categorise( mRomsList[ mCurrentSelection ]->mSettings.GameName.c_str() );
	}

	return C_NUMBERS;
}

//*************************************************************************************
//
//*************************************************************************************
void IRomSelectorComponent::DrawInfoText(  CUIContext * p_context, s32 y, const char * field_txt, const char * value_txt  )
{
	c32			colour(	p_context->GetDefaultTextColour() );

	p_context->DrawTextAlign( TEXT_AREA_LEFT, TEXT_AREA_LEFT + TEXT_AREA_WIDTH, AT_LEFT, y, field_txt, colour );
	p_context->DrawTextAlign( TEXT_AREA_LEFT, TEXT_AREA_LEFT + TEXT_AREA_WIDTH, AT_RIGHT, y, value_txt, colour );
}

//*************************************************************************************
//
//*************************************************************************************
void IRomSelectorComponent::RenderPreview()
{
	const char * noimage = "No Image Available";
	c32	clrGREY = c32( 195, 195, 195, 0 );
	c32	clrORANGE = c32( 255, 128, 0, 0 );
	c32	clrYELLOW = c32( 255, 255, 0, 0 );

	romselmenufs++;
	if (romselmenufs >= 3000) { romselmenufs = 51; }
	if (romselmenuani == 1) {
		romselmenufs = 1;
		romselmenuani = 0; 
	}
	
	if (romselmenufs < 31) {
		if ((romselmenufs < 2) && (romselmenudir == 1)) { ICON_AREA_LEFT = -460; TEXT_AREA_LEFT = -180; }
		else if ((romselmenufs < 4) && (romselmenudir == 1)) { ICON_AREA_LEFT = -428; TEXT_AREA_LEFT = -148; }
		else if ((romselmenufs < 6) && (romselmenudir == 1)) { ICON_AREA_LEFT = -396; TEXT_AREA_LEFT = -116; }
		else if ((romselmenufs < 8) && (romselmenudir == 1)) { ICON_AREA_LEFT = -364; TEXT_AREA_LEFT = -84; }
		else if ((romselmenufs < 10) && (romselmenudir == 1)) { ICON_AREA_LEFT = -332; TEXT_AREA_LEFT = -52; }
		else if ((romselmenufs < 12) && (romselmenudir == 1)) { ICON_AREA_LEFT = -300; TEXT_AREA_LEFT = -20; }
		else if ((romselmenufs < 14) && (romselmenudir == 1)) { ICON_AREA_LEFT = -268; TEXT_AREA_LEFT = 12; }
		else if ((romselmenufs < 16) && (romselmenudir == 1)) { ICON_AREA_LEFT = -236; TEXT_AREA_LEFT = 44; }
		else if ((romselmenufs < 18) && (romselmenudir == 1)) { ICON_AREA_LEFT = -204; TEXT_AREA_LEFT = 76; }
		else if ((romselmenufs < 20) && (romselmenudir == 1)) { ICON_AREA_LEFT = -172; TEXT_AREA_LEFT = 108; }
		else if ((romselmenufs < 22) && (romselmenudir == 1)) { ICON_AREA_LEFT = -140; TEXT_AREA_LEFT = 140; }
		else if ((romselmenufs < 24) && (romselmenudir == 1)) { ICON_AREA_LEFT = -108; TEXT_AREA_LEFT = 172; }
		else if ((romselmenufs < 26) && (romselmenudir == 1)) { ICON_AREA_LEFT = -76; TEXT_AREA_LEFT = 204; }
		else if ((romselmenufs < 28) && (romselmenudir == 1)) { ICON_AREA_LEFT = -44; TEXT_AREA_LEFT = 236; }
		else if ((romselmenufs < 30) && (romselmenudir == 1)) { ICON_AREA_LEFT = -12; TEXT_AREA_LEFT = 268; }
		if ((romselmenufs < 2) && (romselmenudir == 2)) { ICON_AREA_LEFT = 500; TEXT_AREA_LEFT = 780; }
		else if ((romselmenufs < 4) && (romselmenudir == 2)) { ICON_AREA_LEFT = 468; TEXT_AREA_LEFT = 748; }
		else if ((romselmenufs < 6) && (romselmenudir == 2)) { ICON_AREA_LEFT = 436; TEXT_AREA_LEFT = 716; }
		else if ((romselmenufs < 8) && (romselmenudir == 2)) { ICON_AREA_LEFT = 404; TEXT_AREA_LEFT = 684; }
		else if ((romselmenufs < 10) && (romselmenudir == 2)) { ICON_AREA_LEFT = 372; TEXT_AREA_LEFT = 652; }
		else if ((romselmenufs < 12) && (romselmenudir == 2)) { ICON_AREA_LEFT = 340; TEXT_AREA_LEFT = 620; }
		else if ((romselmenufs < 14) && (romselmenudir == 2)) { ICON_AREA_LEFT = 308; TEXT_AREA_LEFT = 588; }
		else if ((romselmenufs < 16) && (romselmenudir == 2)) { ICON_AREA_LEFT = 276; TEXT_AREA_LEFT = 556; }
		else if ((romselmenufs < 18) && (romselmenudir == 2)) { ICON_AREA_LEFT = 244; TEXT_AREA_LEFT = 524; }
		else if ((romselmenufs < 20) && (romselmenudir == 2)) { ICON_AREA_LEFT = 212; TEXT_AREA_LEFT = 492; }
		else if ((romselmenufs < 22) && (romselmenudir == 2)) { ICON_AREA_LEFT = 180; TEXT_AREA_LEFT = 460; }
		else if ((romselmenufs < 24) && (romselmenudir == 2)) { ICON_AREA_LEFT = 148; TEXT_AREA_LEFT = 428; }
		else if ((romselmenufs < 26) && (romselmenudir == 2)) { ICON_AREA_LEFT = 116; TEXT_AREA_LEFT = 396; }
		else if ((romselmenufs < 28) && (romselmenudir == 2)) { ICON_AREA_LEFT = 84; TEXT_AREA_LEFT = 364; }
		else if ((romselmenufs < 30) && (romselmenudir == 2)) { ICON_AREA_LEFT = 52; TEXT_AREA_LEFT = 332; }

		if ( mCurrentSelection < mRomsList.size() ) {
			if ((romselmenufs > 13) && (romselmenudir == 1)) {
			mpContext->DrawRect( ICON_AREA_LEFT-2, ICON_AREA_TOP-2, ICON_AREA_WIDTH+4, ICON_AREA_HEIGHT+4, c32::White );
			mpContext->DrawRect( ICON_AREA_LEFT-1, ICON_AREA_TOP-1, ICON_AREA_WIDTH+2, ICON_AREA_HEIGHT+2, mpContext->GetBackgroundColour() ); 
			}
			else if (romselmenudir == 2) {
			mpContext->DrawRect( ICON_AREA_LEFT-2, ICON_AREA_TOP-2, ICON_AREA_WIDTH+4, ICON_AREA_HEIGHT+4, c32::White );
			mpContext->DrawRect( ICON_AREA_LEFT-1, ICON_AREA_TOP-1, ICON_AREA_WIDTH+2, ICON_AREA_HEIGHT+2, mpContext->GetBackgroundColour() ); 
			}

			v2	tl( ICON_AREA_LEFT, ICON_AREA_TOP );
			v2	wh( ICON_AREA_WIDTH, ICON_AREA_HEIGHT );

			if( mpPreviewTexture != NULL ) {
				c32		colour( c32::White );
				mpContext->DrawRect( ICON_AREA_LEFT, ICON_AREA_TOP, ICON_AREA_WIDTH, ICON_AREA_HEIGHT, c32::Black );
			}
			else {
				mpContext->DrawRect( ICON_AREA_LEFT, ICON_AREA_TOP, ICON_AREA_WIDTH, ICON_AREA_HEIGHT, c32::Black );
				mpContext->DrawText( (ICON_AREA_LEFT + (ICON_AREA_WIDTH / 2)) - (mpContext->GetTextWidth(noimage) / 2), ICON_AREA_TOP + (ICON_AREA_HEIGHT / 2), noimage, mpContext->GetDefaultTextColour() );
			}
		}

		u32		font_height( mpContext->GetFontHeight() );
		u32		line_height( font_height + 2 );

		s32 y = TEXT_AREA_TOP;

		if( mCurrentSelection < mRomsList.size() )
		{
			SRomInfo *	p_rominfo( mRomsList[ mCurrentSelection ] );

			const char *	cic_name( ROM_GetCicName( p_rominfo->mCicType ) );
			const char *	country( ROM_GetCountryNameFromID( p_rominfo->mRomID.CountryID ) );
			u32				rom_size( p_rominfo->mRomSize );

			char buffer[ 32 ];
			sprintf( buffer, "%d MB", rom_size / (1024*1024) ); 

			DrawInfoText( mpContext, y, "Boot:", cic_name );	y += line_height + 5;
			DrawInfoText( mpContext, y, "Country:", country );	y += line_height + 5;
			DrawInfoText( mpContext, y, "Size:", buffer );	y += line_height + 5;

			DrawInfoText( mpContext, y, "Save:", ROM_GetSaveTypeName( p_rominfo->mSettings.SaveType ) ); y += line_height + 5;
			DrawInfoText( mpContext, y, "EPak:", ROM_GetExpansionPakUsageName( p_rominfo->mSettings.ExpansionPakUsage ) ); y += line_height + 5;
			DrawInfoText( mpContext, y, "Dynarec:", p_rominfo->mSettings.DynarecSupported ? "Supported" : "Unsupported" ); y += line_height + 15;
					
			if (( p_rominfo->mSettings.Comment[0] != '0' ) &&( p_rominfo->mSettings.Comment[0] != '1' ) && ( p_rominfo->mSettings.Comment[0] != '2' ) && ( p_rominfo->mSettings.Comment[0] != '3' ) && ( p_rominfo->mSettings.Comment[0] != '4' ) && ( p_rominfo->mSettings.Comment[0] != '5' )) {
				DrawInfoText( mpContext, y, "    Compatibility Info", "" ); y += line_height + 5;				
				DrawInfoText( mpContext, y, "       Not Available", "" ); y += line_height + 5; 
			} else if (romselmenufs > 10) {
				if (p_rominfo->mSettings.Comment[0] == '0') {
					DrawInfoText( mpContext, y, "Compatibility:", "" );
					mpContext->DrawRect( TEXT_AREA_LEFT + TEXT_AREA_WIDTH - 10, y - 10, 10, 10, clrGREY ); y += line_height + 5;
				}	else if ( p_rominfo->mSettings.Comment[0] == '1' ) {
					DrawInfoText( mpContext, y, "Compatibility:", "" );
					mpContext->DrawRect( TEXT_AREA_LEFT + TEXT_AREA_WIDTH - 10, y - 10, 10, 10, c32::Red ); y += line_height + 5;
				}	else if ( p_rominfo->mSettings.Comment[0] == '2' ) {
					DrawInfoText( mpContext, y, "Compatibility:", "" );
					mpContext->DrawRect( TEXT_AREA_LEFT + TEXT_AREA_WIDTH - 10, y - 10, 10, 10, clrORANGE ); y += line_height + 5;
				}	else if ( p_rominfo->mSettings.Comment[0] == '3' ) {
					DrawInfoText( mpContext, y, "Compatibility:", "" );
					mpContext->DrawRect( TEXT_AREA_LEFT + TEXT_AREA_WIDTH - 10, y - 10, 10, 10, clrYELLOW ); y += line_height + 5;
				}	else if ( p_rominfo->mSettings.Comment[0] == '4' ) {
					DrawInfoText( mpContext, y, "Compatibility:", "" );
					mpContext->DrawRect( TEXT_AREA_LEFT + TEXT_AREA_WIDTH - 10, y - 10, 10, 10, c32::Green ); y += line_height + 5;
				}	else if ( p_rominfo->mSettings.Comment[0] == '5' ) {
					DrawInfoText( mpContext, y, "Compatibility:", "" );
					mpContext->DrawRect( TEXT_AREA_LEFT + TEXT_AREA_WIDTH - 10, y - 10, 10, 10, c32::Blue ); y += line_height + 5;
				}
					DrawInfoText( mpContext, y, "Hold     for more info.", "" );				
					mpContext->DrawRect( TEXT_AREA_LEFT + 36, y - 8, 7, 7, c32::White );
					mpContext->DrawRect( TEXT_AREA_LEFT + 37, y - 7, 5, 5, c32::Black ); y += line_height + 5;				
			}
		}
		ICON_AREA_LEFT = 20;
		TEXT_AREA_LEFT = 300;
	
	}
	else {		
		if( mCurrentSelection < mRomsList.size() ) {
			mpContext->DrawRect( ICON_AREA_LEFT-2, ICON_AREA_TOP-2, ICON_AREA_WIDTH+4, ICON_AREA_HEIGHT+4, c32::White );
			mpContext->DrawRect( ICON_AREA_LEFT-1, ICON_AREA_TOP-1, ICON_AREA_WIDTH+2, ICON_AREA_HEIGHT+2, mpContext->GetBackgroundColour() );

			v2	tl( ICON_AREA_LEFT, ICON_AREA_TOP );
			v2	wh( ICON_AREA_WIDTH, ICON_AREA_HEIGHT );

			if( mpPreviewTexture != NULL )
			{
				c32		colour( c32::White );

				if ( mPreviewLoadedTime < PREVIEW_FADE_TIME )
				{
					colour = c32( 255, 255, 255, u8( mPreviewLoadedTime * 255.f / PREVIEW_FADE_TIME ) );
				}

				mpContext->DrawRect( ICON_AREA_LEFT, ICON_AREA_TOP, ICON_AREA_WIDTH, ICON_AREA_HEIGHT, c32::Black );
				if (romselmenufs > 46) {
					mpContext->RenderTexture( mpPreviewTexture, tl, wh, colour );
				}
			}
			else
			{
				mpContext->DrawRect( ICON_AREA_LEFT, ICON_AREA_TOP, ICON_AREA_WIDTH, ICON_AREA_HEIGHT, c32::Black );
				mpContext->DrawText( (ICON_AREA_LEFT + (ICON_AREA_WIDTH / 2)) - (mpContext->GetTextWidth(noimage) / 2), ICON_AREA_TOP + (ICON_AREA_HEIGHT / 2), noimage, mpContext->GetDefaultTextColour() );
			}
		}

		u32		font_height( mpContext->GetFontHeight() );
		u32		line_height( font_height + 2 );

		s32 y = TEXT_AREA_TOP;

		if( mCurrentSelection < mRomsList.size() )
		{
			SRomInfo *	p_rominfo( mRomsList[ mCurrentSelection ] );

			const char *	cic_name( ROM_GetCicName( p_rominfo->mCicType ) );
			const char *	country( ROM_GetCountryNameFromID( p_rominfo->mRomID.CountryID ) );
			u32				rom_size( p_rominfo->mRomSize );

			char buffer[ 32 ];
			sprintf( buffer, "%d MB", rom_size / (1024*1024) ); 

			DrawInfoText( mpContext, y, "Boot:", cic_name );	y += line_height + 5; 
			DrawInfoText( mpContext, y, "Country:", country );	y += line_height + 5;
			DrawInfoText( mpContext, y, "Size:", buffer );	y += line_height + 5;

			DrawInfoText( mpContext, y, "Save:", ROM_GetSaveTypeName( p_rominfo->mSettings.SaveType ) ); y += line_height + 5;
			DrawInfoText( mpContext, y, "EPak:", ROM_GetExpansionPakUsageName( p_rominfo->mSettings.ExpansionPakUsage ) ); y += line_height + 5;
			DrawInfoText( mpContext, y, "Dynarec:", p_rominfo->mSettings.DynarecSupported ? "Supported" : "Unsupported" ); y += line_height + 15;

			if (( p_rominfo->mSettings.Comment[0] != '0' ) &&( p_rominfo->mSettings.Comment[0] != '1' ) && ( p_rominfo->mSettings.Comment[0] != '2' ) && ( p_rominfo->mSettings.Comment[0] != '3' ) && ( p_rominfo->mSettings.Comment[0] != '4' ) && ( p_rominfo->mSettings.Comment[0] != '5' )) {
				DrawInfoText( mpContext, y, "    Compatibility Info", "" ); y += line_height + 5;				
				DrawInfoText( mpContext, y, "       Not Available", "" ); y += line_height + 5; 
			} else {
				if ( p_rominfo->mSettings.Comment[0] == '0' ) {
					DrawInfoText( mpContext, y, "Compatibility:", "" );
					mpContext->DrawRect( TEXT_AREA_LEFT + TEXT_AREA_WIDTH - 10, y - 10, 10, 10, clrGREY ); y += line_height + 5;
				}	else if ( p_rominfo->mSettings.Comment[0] == '1' ) {
					DrawInfoText( mpContext, y, "Compatibility:", "" );
					mpContext->DrawRect( TEXT_AREA_LEFT + TEXT_AREA_WIDTH - 10, y - 10, 10, 10, c32::Red ); y += line_height + 5;
				}	else if ( p_rominfo->mSettings.Comment[0] == '2' ) {
					DrawInfoText( mpContext, y, "Compatibility:", "" );
					mpContext->DrawRect( TEXT_AREA_LEFT + TEXT_AREA_WIDTH - 10, y - 10, 10, 10, clrORANGE ); y += line_height + 5;
				}	else if ( p_rominfo->mSettings.Comment[0] == '3' ) {
					DrawInfoText( mpContext, y, "Compatibility:", "" );
					mpContext->DrawRect( TEXT_AREA_LEFT + TEXT_AREA_WIDTH - 10, y - 10, 10, 10, clrYELLOW ); y += line_height + 5;
				}	else if ( p_rominfo->mSettings.Comment[0] == '4' ) {
					DrawInfoText( mpContext, y, "Compatibility:", "" );
					mpContext->DrawRect( TEXT_AREA_LEFT + TEXT_AREA_WIDTH - 10, y - 10, 10, 10, c32::Green ); y += line_height + 5;
				}	else if ( p_rominfo->mSettings.Comment[0] == '5' ) {
					DrawInfoText( mpContext, y, "Compatibility:", "" );
					mpContext->DrawRect( TEXT_AREA_LEFT + TEXT_AREA_WIDTH - 10, y - 10, 10, 10, c32::Blue ); y += line_height + 5;
				}				
					DrawInfoText( mpContext, y, "Hold     for more info.", "" );				
					mpContext->DrawRect( TEXT_AREA_LEFT + 36, y - 8, 7, 7, c32::White );
					mpContext->DrawRect( TEXT_AREA_LEFT + 37, y - 7, 5, 5, c32::Black ); y += line_height + 5;
			}
			if (( p_rominfo->mSettings.Comment[0] == '0' ) || ( p_rominfo->mSettings.Comment[0] == '1' ) || ( p_rominfo->mSettings.Comment[0] == '2' ) || ( p_rominfo->mSettings.Comment[0] == '3' ) || ( p_rominfo->mSettings.Comment[0] == '4' ) || ( p_rominfo->mSettings.Comment[0] == '5' )) {
				if(showmoreinfo) {					
					const char *compatver = p_rominfo->mSettings.Comment + 15;
					y = 44 + line_height;
					mpContext->DrawRect( 100, 40, 280, 192, c32::White );
					mpContext->DrawRect( 102, 42, 276, 188, c32::Black );
					mpContext->DrawTextAlign( 104, 376, AT_LEFT, y, "Recommended Settings:          Alpha", c32::White );					
					mpContext->DrawTextAlign( 104, 376, AT_RIGHT, y, compatver, c32::White );	y += line_height + 15;

					if ( p_rominfo->mSettings.Comment[1] != '0' ) {
						mpContext->DrawTextAlign( 104, 376, AT_LEFT, y, "Texture Update Check:", c32::White );

						if ( p_rominfo->mSettings.Comment[1] == '1' ) {
							mpContext->DrawTextAlign( 104, 376, AT_RIGHT, y, "Every Frame", c32::White );  y += line_height + 5;
						}
						else if ( p_rominfo->mSettings.Comment[1] == '2' ) {
							mpContext->DrawTextAlign( 104, 376, AT_RIGHT, y, "Every 3", c32::White );  y += line_height + 5;
						}
						else if ( p_rominfo->mSettings.Comment[1] == '3' ) {
							mpContext->DrawTextAlign( 104, 376, AT_RIGHT, y, "Every 5", c32::White );  y += line_height + 5;
						}
						else if ( p_rominfo->mSettings.Comment[1] == '4' ) {
							mpContext->DrawTextAlign( 104, 376, AT_RIGHT, y, "Every 10", c32::White );  y += line_height + 5;
						}
						else if ( p_rominfo->mSettings.Comment[1] == '5' ) {
							mpContext->DrawTextAlign( 104, 376, AT_RIGHT, y, "Every 15", c32::White );  y += line_height + 5;
						}
						else if ( p_rominfo->mSettings.Comment[1] == '6' ) {
							mpContext->DrawTextAlign( 104, 376, AT_RIGHT, y, "Every 20", c32::White );  y += line_height + 5;
						}
						else if ( p_rominfo->mSettings.Comment[1] == '7' ) {
							mpContext->DrawTextAlign( 104, 376, AT_RIGHT, y, "Every 30", c32::White );  y += line_height + 5;
						}
					}
					if ( p_rominfo->mSettings.Comment[2] != '0' ) {
						mpContext->DrawTextAlign( 104, 376, AT_LEFT, y, "FrameSkip:", c32::White );

						if ( p_rominfo->mSettings.Comment[2] == '1' ) {
							mpContext->DrawTextAlign( 104, 376, AT_RIGHT, y, "1", c32::White );  y += line_height + 5;
						}						
						else if ( p_rominfo->mSettings.Comment[2] == '2' ) {
							mpContext->DrawTextAlign( 104, 376, AT_RIGHT, y, "2", c32::White );  y += line_height + 5;
						}
						else if ( p_rominfo->mSettings.Comment[2] == '3' ) {
							mpContext->DrawTextAlign( 104, 376, AT_RIGHT, y, "3", c32::White );  y += line_height + 5;
						}
						else if ( p_rominfo->mSettings.Comment[2] == '4' ) {
							mpContext->DrawTextAlign( 104, 376, AT_RIGHT, y, "5", c32::White );  y += line_height + 5;
						}
						else if ( p_rominfo->mSettings.Comment[2] == '5' ) {
							mpContext->DrawTextAlign( 104, 376, AT_RIGHT, y, "7", c32::White );  y += line_height + 5;
						}
						else if ( p_rominfo->mSettings.Comment[2] == '6' ) {
							mpContext->DrawTextAlign( 104, 376, AT_RIGHT, y, "10", c32::White );  y += line_height + 5;
						}

					}
					if ( p_rominfo->mSettings.Comment[3] != '0' ) {
						mpContext->DrawTextAlign( 104, 376, AT_LEFT, y, "Limit Framerate:", c32::White );

						if ( p_rominfo->mSettings.Comment[3] == '1' ) {
							mpContext->DrawTextAlign( 104, 376, AT_RIGHT, y, "Yes", c32::White );  y += line_height + 5;
						}						
						else if ( p_rominfo->mSettings.Comment[3] == '2' ) {
							mpContext->DrawTextAlign( 104, 376, AT_RIGHT, y, "No", c32::White );  y += line_height + 5;
						}
					}
					if ( p_rominfo->mSettings.Comment[4] != '0' ) {
						mpContext->DrawTextAlign( 104, 376, AT_LEFT, y, "Dynamic Recompilation:", c32::White );

						if ( p_rominfo->mSettings.Comment[4] == '1' ) {
							mpContext->DrawTextAlign( 104, 376, AT_RIGHT, y, "Enabled", c32::White );  y += line_height + 5;
						}						
						else if ( p_rominfo->mSettings.Comment[4] == '2' ) {
							mpContext->DrawTextAlign( 104, 376, AT_RIGHT, y, "Disabled", c32::White );  y += line_height + 5;
						}
					}
					if ( p_rominfo->mSettings.Comment[5] != '0' ) {
						mpContext->DrawTextAlign( 104, 376, AT_LEFT, y, "Dynamic Stack Optimisation:", c32::White );

						if ( p_rominfo->mSettings.Comment[5] == '1' ) {
							mpContext->DrawTextAlign( 104, 376, AT_RIGHT, y, "Enabled", c32::White );  y += line_height + 5;
						}						
						else if ( p_rominfo->mSettings.Comment[5] == '2' ) {
							mpContext->DrawTextAlign( 104, 376, AT_RIGHT, y, "Disabled", c32::White );  y += line_height + 5;
						}
					}
					if ( p_rominfo->mSettings.Comment[6] != '0' ) {
						mpContext->DrawTextAlign( 104, 376, AT_LEFT, y, "High Level Emulation:", c32::White );

						if ( p_rominfo->mSettings.Comment[6] == '1' ) {
							mpContext->DrawTextAlign( 104, 376, AT_RIGHT, y, "Enabled", c32::White );  y += line_height + 5;
						}						
						else if ( p_rominfo->mSettings.Comment[6] == '2' ) {
							mpContext->DrawTextAlign( 104, 376, AT_RIGHT, y, "Disabled", c32::White );  y += line_height + 5;
						}
					}
					if ( p_rominfo->mSettings.Comment[7] != '0' ) {
						mpContext->DrawTextAlign( 104, 376, AT_LEFT, y, "Audio:", c32::White );

						if ( p_rominfo->mSettings.Comment[7] == '1' ) {
							mpContext->DrawTextAlign( 104, 376, AT_RIGHT, y, "Async", c32::White );  y += line_height + 5;
						}						
						else if ( p_rominfo->mSettings.Comment[7] == '2' ) {
							mpContext->DrawTextAlign( 104, 376, AT_RIGHT, y, "Sync", c32::White );  y += line_height + 5;
						}
						else if ( p_rominfo->mSettings.Comment[7] == '3' ) {
							mpContext->DrawTextAlign( 104, 376, AT_RIGHT, y, "Disabled", c32::White );  y += line_height + 5;
						}
					}
					if ( p_rominfo->mSettings.Comment[8] != '0' ) {
						mpContext->DrawTextAlign( 104, 376, AT_LEFT, y, "Clean Scene:", c32::White );

						if ( p_rominfo->mSettings.Comment[8] == '1' ) {
							mpContext->DrawTextAlign( 104, 376, AT_RIGHT, y, "Enabled", c32::White );  y += line_height + 5;
						}						
						else if ( p_rominfo->mSettings.Comment[8] == '2' ) {
							mpContext->DrawTextAlign( 104, 376, AT_RIGHT, y, "Disabled", c32::White );  y += line_height + 5;
						}
					}
					if ( p_rominfo->mSettings.Comment[9] != '0' ) {
						mpContext->DrawTextAlign( 104, 376, AT_LEFT, y, "Use Flushtris:", c32::White );

						if ( p_rominfo->mSettings.Comment[9] == '1' ) {
							mpContext->DrawTextAlign( 104, 376, AT_RIGHT, y, "Yes", c32::White );  y += line_height + 5;
						}						
						else if ( p_rominfo->mSettings.Comment[9] == '2' ) {
							mpContext->DrawTextAlign( 104, 376, AT_RIGHT, y, "No", c32::White );  y += line_height + 5;
						}
					}
					if ( p_rominfo->mSettings.Comment[10] != '0' ) {
						mpContext->DrawTextAlign( 104, 376, AT_LEFT, y, "Dynamic Loop Optimisation:", c32::White );

						if ( p_rominfo->mSettings.Comment[10] == '1' ) {
							mpContext->DrawTextAlign( 104, 376, AT_RIGHT, y, "Enabled", c32::White );  y += line_height + 5;
						}						
						else if ( p_rominfo->mSettings.Comment[10] == '2' ) {
							mpContext->DrawTextAlign( 104, 376, AT_RIGHT, y, "Disabled", c32::White );  y += line_height + 5;
						}
					}
					if ( p_rominfo->mSettings.Comment[11] != '0' ) {
						mpContext->DrawTextAlign( 104, 376, AT_LEFT, y, "Double Display Lists:", c32::White );

						if ( p_rominfo->mSettings.Comment[11] == '1' ) {
							mpContext->DrawTextAlign( 104, 376, AT_RIGHT, y, "Enabled", c32::White );  y += line_height + 5;
						}						
						else if ( p_rominfo->mSettings.Comment[11] == '2' ) {
							mpContext->DrawTextAlign( 104, 376, AT_RIGHT, y, "Disabled", c32::White );  y += line_height + 5;
						}
					}
					if ( p_rominfo->mSettings.Comment[12] != '0' ) {
						mpContext->DrawTextAlign( 104, 376, AT_LEFT, y, "View Port Hack:", c32::White );

						if ( p_rominfo->mSettings.Comment[12] == '1' ) {
							mpContext->DrawTextAlign( 104, 376, AT_RIGHT, y, "Enabled", c32::White );  y += line_height + 5;
						}						
						else if ( p_rominfo->mSettings.Comment[12] == '2' ) {
							mpContext->DrawTextAlign( 104, 376, AT_RIGHT, y, "Disabled", c32::White );  y += line_height + 5;
						}
					}
					if ( p_rominfo->mSettings.Comment[13] != '0' ) {
						mpContext->DrawTextAlign( 104, 376, AT_LEFT, y, "Disable Flat Shade:", c32::White );

						if ( p_rominfo->mSettings.Comment[13] == '1' ) {
							mpContext->DrawTextAlign( 104, 376, AT_RIGHT, y, "Yes", c32::White );  y += line_height + 5;
						}						
						else if ( p_rominfo->mSettings.Comment[13] == '2' ) {
							mpContext->DrawTextAlign( 104, 376, AT_RIGHT, y, "No", c32::White );  y += line_height + 5;
						}
					}
					if ( p_rominfo->mSettings.Comment[14] != '0' ) {
						mpContext->DrawTextAlign( 104, 376, AT_LEFT, y, "Disable Simulate Double:", c32::White );

						if ( p_rominfo->mSettings.Comment[14] == '1' ) {
							mpContext->DrawTextAlign( 104, 376, AT_RIGHT, y, "Yes", c32::White );  y += line_height + 5;
						}						
						else if ( p_rominfo->mSettings.Comment[14] == '2' ) {
							mpContext->DrawTextAlign( 104, 376, AT_RIGHT, y, "No", c32::White );  y += line_height + 5;
						}
					}
				}
			}
		}
	}
}

//*************************************************************************************
//
//*************************************************************************************
void IRomSelectorComponent::RenderRomList()
{		
	const char *	pre3p_gamename = NULL;
	const char *	pre2p_gamename = NULL;
	const char *	prevp_gamename = NULL;
	const char *	p_gamename;
	const char *	nextp_gamename = NULL;
	const char *	nex2p_gamename = NULL;	
	const char *	nex3p_gamename = NULL;
	c32		colour;
	
	if( mDisplayFilenames )
	{		
		if (mCurrentSelection > 2) {
			pre3p_gamename = mRomsList[ mCurrentSelection - 3 ]->mFilename.c_str();
		}
		if (mCurrentSelection > 1) {
			pre2p_gamename = mRomsList[ mCurrentSelection - 2 ]->mFilename.c_str();
		}
		if (mCurrentSelection > 0) {
			prevp_gamename = mRomsList[ mCurrentSelection - 1 ]->mFilename.c_str();
		}
		
		p_gamename = mRomsList[ mCurrentSelection ]->mFilename.c_str();
		
		if (mCurrentSelection < mRomsList.size() - 1) {
			nextp_gamename = mRomsList[ mCurrentSelection + 1 ]->mFilename.c_str();
		}
		if (mCurrentSelection < mRomsList.size() - 2) {
			nex2p_gamename = mRomsList[ mCurrentSelection + 2 ]->mFilename.c_str();
		}
		if (mCurrentSelection < mRomsList.size() - 3) {
			nex3p_gamename = mRomsList[ mCurrentSelection + 3 ]->mFilename.c_str();
		}
	}
	else
	{
		if (mCurrentSelection > 2) {
			pre3p_gamename = mRomsList[ mCurrentSelection - 3 ]->mSettings.GameName.c_str();
		}
		if (mCurrentSelection > 1) {
			pre2p_gamename = mRomsList[ mCurrentSelection - 2 ]->mSettings.GameName.c_str();
		}
		if (mCurrentSelection > 0) {
			prevp_gamename = mRomsList[ mCurrentSelection - 1 ]->mSettings.GameName.c_str();
		}
		
		p_gamename = mRomsList[ mCurrentSelection ]->mSettings.GameName.c_str();
		
		if (mCurrentSelection < mRomsList.size() - 1) {
			nextp_gamename = mRomsList[ mCurrentSelection + 1 ]->mSettings.GameName.c_str();
		}
		if (mCurrentSelection < mRomsList.size() - 2) {
			nex2p_gamename = mRomsList[ mCurrentSelection + 2 ]->mSettings.GameName.c_str();
		}
		if (mCurrentSelection < mRomsList.size() - 3) {
			nex3p_gamename = mRomsList[ mCurrentSelection + 3 ]->mSettings.GameName.c_str();
		}
	}
		
	if (romselmenufs < 31)  {		
		if (romselmenudir == 1) {			
			if (romseltextoffset == 0) {
				romseltextoffset = 20 + ((mpContext->GetTextWidth( nextp_gamename ) * 1.2) / 2) + ((mpContext->GetTextWidth( p_gamename ) * 0.8) / 2);
				romseltextrepos = (romseltextoffset / 30);
				romseltextscale = (0.4 / 30);
			}			
			if (mCurrentSelection > 1)
			{
				colour = mpContext->GetDefaultTextColour();
				mpContext->DrawTextScale( 200 - (((mpContext->GetTextWidth( p_gamename ) * (0.8 + (romseltextscale * romselmenufs)) / 2)) + (mpContext->GetTextWidth( prevp_gamename ) * 0.8) + (mpContext->GetTextWidth( pre2p_gamename ) * 0.8)) - romseltextoffset, 260, 0.8, pre2p_gamename, colour );
			}
			if (mCurrentSelection > 0)
			{
				colour = mpContext->GetDefaultTextColour();
				mpContext->DrawTextScale( 220 - (((mpContext->GetTextWidth( p_gamename ) * (0.8 + (romseltextscale * romselmenufs)) / 2)) + (mpContext->GetTextWidth( prevp_gamename ) * 0.8)) - romseltextoffset, 260, 0.8, prevp_gamename, colour );
			}

			colour = mpContext->GetSelectedTextColour();
			mpContext->DrawTextScale( 240 - ((mpContext->GetTextWidth( p_gamename ) * (0.8 + (romseltextscale * romselmenufs)) / 2)) - romseltextoffset, 260, 0.8 + (romseltextscale * romselmenufs),p_gamename, colour );	
			
			colour = mpContext->GetDefaultTextColour();
			mpContext->DrawTextScale( 260 + ((mpContext->GetTextWidth( p_gamename ) * (0.8 + (romseltextscale * romselmenufs)) / 2)) - romseltextoffset, 260, 1.2 - (romseltextscale * romselmenufs),nextp_gamename, colour );
			
			if (mCurrentSelection < mRomsList.size() - 2)
			{
				colour = mpContext->GetDefaultTextColour();
				mpContext->DrawTextScale( 280 + (((mpContext->GetTextWidth( p_gamename ) * (0.8 + (romseltextscale * romselmenufs)) / 2)) + ((mpContext->GetTextWidth( nextp_gamename )) * (1.2 - (romseltextscale * romselmenufs)))) - romseltextoffset, 260, 0.8, nex2p_gamename, colour );
			}
			if (mCurrentSelection < mRomsList.size() - 3)
			{
				colour = mpContext->GetDefaultTextColour();
				mpContext->DrawTextScale( 300 + (((mpContext->GetTextWidth( p_gamename ) * (0.8 + (romseltextscale * romselmenufs)) / 2)) + ((mpContext->GetTextWidth( nextp_gamename )) * (1.2 - (romseltextscale * romselmenufs))) + (mpContext->GetTextWidth( nex2p_gamename ) * 0.8)) - romseltextoffset, 260, 0.8, nex3p_gamename, colour );
			}
	
			romseltextoffset -= romseltextrepos;
		}
		else if (romselmenudir == 2) 
		{
			
			if (romseltextoffset == 0) {
				romseltextoffset = 20 + ((mpContext->GetTextWidth( prevp_gamename ) * 1.2) / 2) + ((mpContext->GetTextWidth( p_gamename ) * 0.8) / 2);
				romseltextrepos = (romseltextoffset / 30);
				romseltextscale = (0.4 / 30);
			}			
			if (mCurrentSelection > 2)
			{
				colour = mpContext->GetDefaultTextColour();
				mpContext->DrawTextScale( 180 - (((mpContext->GetTextWidth( p_gamename ) * (0.8 + (romseltextscale * romselmenufs)) / 2)) + (mpContext->GetTextWidth( prevp_gamename ) * 0.8) + (mpContext->GetTextWidth( pre2p_gamename ) * 0.8) + (mpContext->GetTextWidth( pre3p_gamename ) * 0.8)) + romseltextoffset, 260, 0.8, pre3p_gamename, colour );
			}
			if (mCurrentSelection > 1)
			{
				colour = mpContext->GetDefaultTextColour();
				mpContext->DrawTextScale( 200 - (((mpContext->GetTextWidth( p_gamename ) * (0.8 + (romseltextscale * romselmenufs)) / 2)) + (mpContext->GetTextWidth( prevp_gamename ) * 0.8) + (mpContext->GetTextWidth( pre2p_gamename ) * 0.8)) + romseltextoffset, 260, 0.8, pre2p_gamename, colour );
			}			
			if (mCurrentSelection > 0)
			{
				colour = mpContext->GetDefaultTextColour();
				mpContext->DrawTextScale( 220 - (((mpContext->GetTextWidth( p_gamename ) * (0.8 + (romseltextscale * romselmenufs)) / 2)) + (mpContext->GetTextWidth( prevp_gamename ) * 0.8)) + romseltextoffset, 260, 0.8, prevp_gamename, colour );
			}

			colour = mpContext->GetSelectedTextColour();
			mpContext->DrawTextScale( 240 - ((mpContext->GetTextWidth( p_gamename ) * (0.8 + (romseltextscale * romselmenufs)) / 2)) + romseltextoffset, 260, 0.8 + (romseltextscale * romselmenufs),p_gamename, colour );	
			
			if (mCurrentSelection < mRomsList.size() - 1)
			{
				colour = mpContext->GetDefaultTextColour();
				mpContext->DrawTextScale( 260 + ((mpContext->GetTextWidth( p_gamename ) * (0.8 + (romseltextscale * romselmenufs)) / 2)) + romseltextoffset, 260, 1.2 - (romseltextscale * romselmenufs),nextp_gamename, colour );
			}

			if (mCurrentSelection < mRomsList.size() - 2)
			{
				colour = mpContext->GetDefaultTextColour();
				mpContext->DrawTextScale( 280 + (((mpContext->GetTextWidth( p_gamename ) * (0.8 + (romseltextscale * romselmenufs)) / 2)) + ((mpContext->GetTextWidth( nextp_gamename )) * (1.2 - (romseltextscale * romselmenufs)))) + romseltextoffset, 260, 0.8, nex2p_gamename, colour );
			}
	
			romseltextoffset -= romseltextrepos;
		}
	}	
	else if (mRomsList.size() == 1) {
		colour = mpContext->GetSelectedTextColour();
		mpContext->DrawTextScale( 240 - ((mpContext->GetTextWidth( p_gamename ) * 1.2) / 2), 260, 1.2, p_gamename, colour );	
	}
	else {
		if (mCurrentSelection > 1)
		{
			colour = mpContext->GetDefaultTextColour();
			mpContext->DrawTextScale( 200 - (((mpContext->GetTextWidth( p_gamename ) * 1.2) / 2) + (mpContext->GetTextWidth( prevp_gamename ) * 0.8) + (mpContext->GetTextWidth( pre2p_gamename ) * 0.8)), 260, 0.8, pre2p_gamename, colour );
		}
		if (mCurrentSelection > 0)
		{
			colour = mpContext->GetDefaultTextColour();
			mpContext->DrawTextScale( 220 - (((mpContext->GetTextWidth( p_gamename ) * 1.2) / 2) + (mpContext->GetTextWidth( prevp_gamename ) * 0.8)), 260, 0.8, prevp_gamename, colour );
		}

		colour = mpContext->GetSelectedTextColour();
		mpContext->DrawTextScale( 240 - ((mpContext->GetTextWidth( p_gamename ) * 1.2) / 2) ,260 , 1.2, p_gamename, colour );	
		
		if (mCurrentSelection < mRomsList.size() - 1)
		{
			colour = mpContext->GetDefaultTextColour();
			mpContext->DrawTextScale( 260 + ((mpContext->GetTextWidth( p_gamename ) * 1.2) / 2) , 260, 0.8, nextp_gamename, colour );
		}
		if (mCurrentSelection < mRomsList.size() - 2)
		{
			colour = mpContext->GetDefaultTextColour();
			mpContext->DrawTextScale( 280 + (((mpContext->GetTextWidth( p_gamename ) * 1.2) / 2) + (mpContext->GetTextWidth( nextp_gamename ) * 0.8)), 260, 0.8, nex2p_gamename, colour );
		}
	}
}
//*************************************************************************************
//
//*************************************************************************************
void IRomSelectorComponent::RenderCategoryList()
{
	s32 centerx = CATEGORY_AREA_LEFT;
	s32 x = CATEGORY_AREA_LEFT;
	s32 y = CATEGORY_AREA_TOP;
	char selstr[16];
	char str[16];
	float centerwidth = 0.0f;
	float catwidth = 0.0f;
	int centercategory = 1;	
	int prevcategory = 1;
	

	ECategory current_category( GetCurrentCategory() );
	if (romselmenufs < 31) {	
		if (!isnextset) {
			for( int i = 0; i < NUM_CATEGORIES; ++i ) {
				ECategory	category = ECategory( i );
				if ((i > -1) && (i < NUM_CATEGORIES)) {
					sprintf( str, " %c ", GetCategoryLetter( category ) );
					centerwidth += mpContext->GetTextWidth( str );
				}
				AlphaMap::const_iterator it( mRomCategoryMap.find( category ) );
				if ((it != mRomCategoryMap.end()) && ( current_category == category )) {
					sprintf( selstr, " %c ", GetCategoryLetter( category ) );
					prevcategory = i;
					break;
				}
			}
			if (romselmenudir == 1) {
				for( int i = prevcategory - 1; i > -1; --i )	{
					ECategory	category = ECategory( i );				
					AlphaMap::const_iterator it( mRomCategoryMap.find( category ) );
					if( it != mRomCategoryMap.end() ) {						
						sprintf( str, " %c ", GetCategoryLetter( category ) );	
						catwidth += mpContext->GetTextWidth( str );
						isnextset = 1;
						break;
					} else {
						sprintf( str, " %c ", GetCategoryLetter( category ) );
						catwidth += mpContext->GetTextWidth( str );
					}
				}
			} else if (romselmenudir == 2) {
				for( u32 i = prevcategory + 1; i < NUM_CATEGORIES; ++i ) {
					ECategory	category = ECategory( i );
					AlphaMap::const_iterator it( mRomCategoryMap.find( category ) );
					if( it != mRomCategoryMap.end() ) {						
						sprintf( str, " %c ", GetCategoryLetter( category ) );	
						catwidth += mpContext->GetTextWidth( str );
						isnextset = 1;
						break;
					} else {
						sprintf( str, " %c ", GetCategoryLetter( category ) );
						catwidth += mpContext->GetTextWidth( str );
					}
				}
			}
			if (romselmenudir == 1) {		
				// Search for the next valid predecessor
				while(current_category > 0)	{				
					current_category = ECategory( current_category - 1 );
					AlphaMap::const_iterator it( mRomCategoryMap.find( current_category ) );
					if ( it != mRomCategoryMap.end() ) {
						mCurrentSelection = it->second;		
						break;
					}
				}
				romseltextoffset = (240 - centerwidth) + 5;
				romseltextrepos = catwidth / 30.0f;
				romseltextscale = (0.4 / 30);
			}
			if (romselmenudir == 2) {		
				// Search for the next valid predecessor	
				while(current_category < NUM_CATEGORIES-1) {
					current_category = ECategory( current_category + 1 );
					AlphaMap::const_iterator it( mRomCategoryMap.find( current_category ) );
					if( it != mRomCategoryMap.end() ) {
						mCurrentSelection = it->second;		
						break;
					}
				}
				romseltextoffset = (240 - centerwidth) + 5;
				romseltextrepos = catwidth / 30.0f;
				romseltextscale = (0.4 / 30);
			}
		}
		if (romselmenudir == 1) { 						
			romseltextoffset += romseltextrepos;
			mpContext->DrawText(romseltextoffset, y, catstr, c32(180, 180, 180)); 
		}
		else if (romselmenudir == 2) { 	
			romseltextoffset -= romseltextrepos;
			mpContext->DrawText(romseltextoffset, y, catstr, c32(180, 180, 180)); 
		}

	} else {
		centerx = CATEGORY_AREA_LEFT;

		ECategory current_category( GetCurrentCategory() );
		for( u32 i = 0; i < NUM_CATEGORIES; ++i )
		{
			ECategory	category = ECategory( i );
			c32			colour;

			AlphaMap::const_iterator it( mRomCategoryMap.find( category ) );
			if ((it != mRomCategoryMap.end()) && ( current_category == category )) {
				colour = mpContext->GetSelectedTextColour();
				sprintf( str, " %c ", GetCategoryLetter( category ) );
				centercategory = i;
				centerx -= (mpContext->GetTextWidth( str ) / 2); 
				mpContext->DrawText( centerx, y, str, colour );
				break;
			}
		}

		x = centerx;

		for( int i = centercategory; i > -1; --i )
		{
			ECategory	category = ECategory( i );
			c32			colour;

			if( current_category == category ) { 
				sprintf( str, " %c ", GetCategoryLetter( category ) ); 
				continue; 
			}
			AlphaMap::const_iterator it( mRomCategoryMap.find( category ) );
			if( it != mRomCategoryMap.end() ) {
				colour = mpContext->GetDefaultTextColour();
			} else {
				colour = c32( 180, 180, 180 );
			}

			sprintf( str, " %c ", GetCategoryLetter( category ) );
			x -= mpContext->GetTextWidth(str);
			mpContext->DrawText( x, y, str, colour );
		}

		x = centerx;

		for( u32 i = centercategory; i < NUM_CATEGORIES; ++i )
		{
			ECategory	category = ECategory( i );
			c32			colour;
			
			if( current_category == category ) { 
				sprintf( str, " %c ", GetCategoryLetter( category ) ); 
				x += mpContext->GetTextWidth( str ); 
				continue; 
			}
			AlphaMap::const_iterator it( mRomCategoryMap.find( category ) );
			if( it != mRomCategoryMap.end() ) {			
				colour = mpContext->GetDefaultTextColour();
			} else {
				colour = c32( 180, 180, 180 );
			}

			sprintf( str, " %c ", GetCategoryLetter( category ) );
			x += mpContext->DrawText( x, y, str, colour );
		}
	}
}

//*************************************************************************************
//
//*************************************************************************************
void IRomSelectorComponent::Render()
{
	RenderPreview();

	if( mRomsList.empty() )	{
		s32 offset( 0 );
		for( u32 i = 0; i < ARRAYSIZE( gNoRomsText ); ++i )	{
			mpContext->DrawText(240 - (mpContext->GetTextWidth( gNoRomsText[ i ]) / 2 ), 75 + offset, gNoRomsText[ i ], c32::White );
			offset += 10;
			if ((i == 0) || (i == 4)) { offset += 10; }
		}
	} else if (!sortbyletter) {
		RenderRomList();
	} else if (sortbyletter) { 
		RenderCategoryList();
	}
	
	if(mQuitTriggered)
		mpContext->DrawTextAlign(0,480,AT_CENTRE,135,"Press X to quit",
				DrawTextUtilities::TextRed,DrawTextUtilities::TextWhite);
}

//*************************************************************************************
//
//*************************************************************************************
void	IRomSelectorComponent::Update( float elapsed_time, const v2 & stick, u32 old_buttons, u32 new_buttons )
{
	static const float	SCROLL_RATE_PER_SECOND = 25.0f;		// 25 roms/second
	
	/*Apply stick deadzone preference in the RomSelector menu*/
	v2 stick_dead(ApplyDeadzone( stick, gGlobalPreferences.StickMinDeadzone, gGlobalPreferences.StickMaxDeadzone ));
	
	mSelectionAccumulator += stick_dead.x * SCROLL_RATE_PER_SECOND * elapsed_time; 
	
	/*Tricky thing to get the stick to work in every cases
	  for the 100/100 case for example
	  without it, the accumulator gets weirdly set to a NaN value and
	  everything is blocked... So it keeps the accumulator out of a NaN value.
	  */
	if( !(mSelectionAccumulator<0) && !(mSelectionAccumulator>0))
	  mSelectionAccumulator=0.0f;

	ECategory current_category( GetCurrentCategory() );

	u32				initial_selection( mCurrentSelection );
		
	if( (new_buttons & PSP_CTRL_CROSS) && mQuitTriggered)
			sceKernelExitGame();

	mDisplayFilenames = (new_buttons & PSP_CTRL_TRIANGLE) != 0;		
	if (new_buttons & PSP_CTRL_CIRCLE) {	
		sortbyletter = 1;		
		if ((new_buttons & PSP_CTRL_LEFT) && !(old_buttons & PSP_CTRL_LEFT)) {	
			// Search for the next valid predecessor
			for( int i = current_category - 1; i > -1; --i ) {
				ECategory	category = ECategory( i );
				AlphaMap::const_iterator it( mRomCategoryMap.find( category ) );
				if( it != mRomCategoryMap.end() )
				{					
					romselmenuani = 1;
					romselmenudir = 1;
					romseltextoffset = 0;
					isnextset = 0;
					break;
				}
			}
		}
		if ((new_buttons & PSP_CTRL_RIGHT) && !(old_buttons & PSP_CTRL_RIGHT)) {	
			for( int i = current_category + 1; i < NUM_CATEGORIES - 1; ++i ) {
				ECategory	category = ECategory( i );
				AlphaMap::const_iterator it( mRomCategoryMap.find( category ) );
				if( it != mRomCategoryMap.end() )
				{					
					romselmenuani = 1;
					romselmenudir = 2;
					romseltextoffset = 0;
					isnextset = 0;
					break;
				}
			}
		}
	}
	else { sortbyletter = 0; }
		
	if(new_buttons & PSP_CTRL_SQUARE) {			
		showmoreinfo = 1;
	}
	else { showmoreinfo = 0; }
	if (old_buttons != new_buttons)	{
		mQuitTriggered=false;
		if (!(new_buttons & PSP_CTRL_CIRCLE)) {
			if (new_buttons & PSP_CTRL_LEFT)
			{
				if(mCurrentSelection > 0)
				{
					mCurrentSelection--;
					romselmenuani = 1;
					romselmenudir = 1;
					romseltextoffset = 0;
				}
			}
			if(new_buttons & PSP_CTRL_RIGHT)
			{
				if(mCurrentSelection < mRomsList.size() - 1)
				{
					mCurrentSelection++;
					romselmenuani = 1;
					romselmenudir = 2;
					romseltextoffset = 0;
				}
			}
		}

		if((new_buttons & PSP_CTRL_START) ||
			(new_buttons & PSP_CTRL_CROSS))
		{
			if(mCurrentSelection < mRomsList.size())
			{
				mSelectedRom = mRomsList[ mCurrentSelection ]->mFilename;

				if(OnRomSelected != NULL)
				{
					(*OnRomSelected)( mSelectedRom.c_str() );
				}
			}
		}	

	}

	// Init our kernel buttons, ex HOME button
	new_kbuttons = getbuttons();

	if(old_buttons != new_kbuttons)
	{
		if(new_kbuttons & PSP_CTRL_HOME) 
		{
			if(!mQuitTriggered)
			{
				mQuitTriggered=true;
			}
		}
	}
	//
	//	Apply the selection accumulator
	//
	f32		current_vel( mSelectionAccumulator );
	while(mSelectionAccumulator >= 1.0f)
	{
		if(mCurrentSelection < mRomsList.size() - 1)
		{
			mCurrentSelection++;
			mQuitTriggered=false;
		}
		mSelectionAccumulator -= 1.0f;
	}
	while(mSelectionAccumulator <= -1.0f)
	{
		if(mCurrentSelection > 0)
		{
			mCurrentSelection--;
			mQuitTriggered=false;
		}
		mSelectionAccumulator += 1.0f;
	}

	//
	//	Scroll to keep things in view
	//	We add on 'current_vel * 2' to keep the selection highlight as close to the
	//	center as possible (as if we're predicting 2 frames ahead)
	//
	const u32		font_height( mpContext->GetFontHeight() );
	const u32		line_height( font_height + 2 );

	if( mRomsList.size() * line_height > TEXT_AREA_HEIGHT )
	{
		s32		current_selection_y = s32((mCurrentSelection + current_vel * 2) * line_height) + (line_height/2) + mCurrentScrollOffset;

		s32		adjust_amount( (TEXT_AREA_HEIGHT/2) - current_selection_y );

		f32 d( 1.0f - vfpu_powf(0.993f, elapsed_time * 1000.0f) );

		u32		total_height( mRomsList.size() * line_height );
		s32		min_offset( TEXT_AREA_HEIGHT - total_height );

		s32	new_scroll_offset = mCurrentScrollOffset + s32(float(adjust_amount) * d);

		mCurrentScrollOffset = Clamp( new_scroll_offset, min_offset, s32(0) );
	}
	else
	{
		mCurrentScrollOffset = 0;
	}

	//
	//	Increase a timer is the current selection is still the same (i.e. if we've not scrolled)
	//
	if( initial_selection == mCurrentSelection )
	{
		mTimeSinceScroll += elapsed_time;
	}
	else
	{
		mTimeSinceScroll = 0;
	}

	//
	//	If the current selection is different from the preview, invalidate the picture.
	//	
	//
	if( mCurrentSelection < mRomsList.size() && mPreviewIdx != mCurrentSelection )
	{
		//mPreviewIdx = u32(-1);

		mPreviewLoadedTime -= elapsed_time;
		if(mPreviewLoadedTime < 0.0f)
			mPreviewLoadedTime = 0.0f;
	
		//
		//	If we've waited long enough since starting to scroll, try and load the preview image
		//	Note that it may fail, so we sort out the other flags regardless.
		//
		if( mTimeSinceScroll > PREVIEW_SCROLL_WAIT )
		{
			mpPreviewTexture = NULL;
			mPreviewLoadedTime = 0.0f;
			mPreviewIdx = mCurrentSelection;

			if( !mRomsList[ mCurrentSelection ]->mSettings.Preview.empty() )
			{
				char		preview_filename[ MAX_PATH + 1 ];
				IO::Path::Combine( preview_filename, gPreviewDirectory, mRomsList[ mCurrentSelection ]->mSettings.Preview.c_str() );
				
				mpPreviewTexture = CNativeTexture::CreateFromPng( preview_filename, TexFmt_8888 );
			}
		}
	}

	//
	//	Once the preview has been loaded, increase a timer to fade us in.
	//
	if( mPreviewIdx == mCurrentSelection )
	{
		mPreviewLoadedTime += elapsed_time;
		if(mPreviewLoadedTime > PREVIEW_FADE_TIME)
			mPreviewLoadedTime = PREVIEW_FADE_TIME;
	}
}
