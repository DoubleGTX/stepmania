#ifndef NOTES_LOADER_H
#define NOTES_LOADER_H

#include "song.h"
#include "RageUtil.h"
#include <set>

typedef int DanceNote;
enum {
	DANCE_NOTE_NONE = 0,
	DANCE_NOTE_PAD1_LEFT,
	DANCE_NOTE_PAD1_UPLEFT,
	DANCE_NOTE_PAD1_DOWN,
	DANCE_NOTE_PAD1_UP,
	DANCE_NOTE_PAD1_UPRIGHT,
	DANCE_NOTE_PAD1_RIGHT,
	DANCE_NOTE_PAD2_LEFT,
	DANCE_NOTE_PAD2_UPLEFT,
	DANCE_NOTE_PAD2_DOWN,
	DANCE_NOTE_PAD2_UP,
	DANCE_NOTE_PAD2_UPRIGHT,
	DANCE_NOTE_PAD2_RIGHT
};

class NotesLoader {
protected:
	virtual void GetApplicableFiles( CString sPath, CStringArray &out )=0;

	set<istring> BlacklistedImages;

public:
	virtual ~NotesLoader() { }
	const set<istring> &GetBlacklistedImages() const { return BlacklistedImages; }
	static void GetMainAndSubTitlesFromFullTitle( const CString sFullTitle, CString &sMainTitleOut, CString &sSubTitleOut );
	virtual bool LoadFromDir( CString sPath, Song &out ) = 0;
	bool Loadable( CString sPath );
};

#endif
