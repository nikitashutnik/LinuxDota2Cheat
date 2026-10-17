#pragma once

namespace Settings
{
	inline bool  autoAccept   = true;
	inline float camDistance  = 0.0f; // 0 = leave the game's camera alone
	inline bool  lastSeen     = true; // enemy last-seen markers on the minimap
	inline int   lastSeenSecs = 10;

	// ~/.config/dota2helper.cfg
	void Load();
	void Save();
}
