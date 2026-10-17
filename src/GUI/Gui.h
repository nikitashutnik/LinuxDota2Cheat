#pragma once

namespace UI
{
	void ToggleUI();
	void Destroy();
	void PollBridge();       // pulls settings / log lines the JS published through panel ids
	void PushMarkers( const char* marksJsArray, const char* hiddenJsObject ); // "[{n:'npc_dota_hero_axe',i:123,x:1.0,y:2.0,z:128,a:3.5},...]", "{123:1,...}" enemy entity indices in the fog
	void UpdateGround();     // re-places the pushed markers on the ground; they follow the camera, so call it every frame
	void RunDevScript();     // DEL: runs /tmp/d2h_run.js in the current UI context
}
