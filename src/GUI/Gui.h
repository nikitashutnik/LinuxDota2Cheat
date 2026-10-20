#pragma once

namespace UI
{
	void ToggleUI();
	void Destroy();
	void PollBridge();       // pulls settings / log lines the JS published through panel ids
	void PushMarkers( const char* marksJsArray, const char* unitsJsArray, const char* barsJsObject, const char* lanesJsObject ); // "[{n:'npc_dota_hero_axe',i:123,x:1.0,y:2.0,z:128,a:3.5},...]", heroes in sight (LastSeen AppendUnit), top bar health/mana (AppendBar), lane buildings and our creeps
	void UpdateGround( const char* positionsJsObject ); // where the heroes under the ground overlay are now; call it once per rendered frame
	void RunDevScript();     // DEL: runs /tmp/d2h_run.js in the current UI context
}
