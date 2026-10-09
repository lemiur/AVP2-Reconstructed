// Jupiter runtime/shared/src/engine_vars.cpp
// This module defines all the engine's console variables.
// NOTE: the reason some of the variables are extern'd is because certain modules don't
// compile with engine_vars.cpp so they 'own' the variable.
#include "bdefs.h"
#include "concommand.h"
#include "engine_vars.h"

#define DEFAULT_CLIENT_UPDATE_RATE	10	// Jupiter shared/src/packetdefs.h

// GLOBAL: LITHTECH 0x004d4840
extern float g_CV_DefaultDrawIndexedDist;

// GLOBAL: LITHTECH 0x004d2128
int32	g_CV_RenderEnable = LTTRUE;

// servermgr: ticks spent in class updates this frame (Jupiter defines it in classmgr.cpp).
// GLOBAL: LITHTECH 0x004e36c0
uint32	g_Ticks_ClassUpdate = 0;

// GLOBAL: LITHTECH 0x004e36c4
int32	g_CV_FlashClients = LTFALSE;
// GLOBAL: LITHTECH 0x004e36c8
int32	g_CV_PlayDemoReps = 0;	// How many times to repeat the PlayDemo.
// GLOBAL: LITHTECH 0x004e36cc
int32	g_CV_PlayDemoProfile = 0;

// GLOBAL: LITHTECH 0x004e36d0
int32	g_CV_ForceNoSound = LTFALSE;

// soundinstance: filtered samples are muted and left looping on their tail instead of being stopped
// when it is cleared ("SoundEndCap").
// GLOBAL: LITHTECH 0x004d212c
LTBOOL	g_bStopFilteredSamples = LTTRUE;
// soundmgr: when a buffer has more instances than this, the one closest to finishing is removed
// ("SoundsPerBuffer").
// GLOBAL: LITHTECH 0x004d2130
uint32	g_dwMaxInstancesPerBuffer = 32;

// GLOBAL: LITHTECH 0x004d2134
int32	g_CV_CursorCenter = LTTRUE;	// Automatically resets the cursor to the center
									// of the window/screen each frame
// GLOBAL: LITHTECH 0x004d2138
LTBOOL	g_CV_CacheFiles = LTTRUE;

// GLOBAL: LITHTECH 0x004e36d4
int32	g_CV_VideoDebug = LTFALSE;

// GLOBAL: LITHTECH 0x004e36d8
int32	g_CV_DebugLoaders = LTFALSE; // Debug output for loader threads?


// GLOBAL: LITHTECH 0x004e36dc
int32	g_CV_STracePackets = LTFALSE;
// GLOBAL: LITHTECH 0x004d213c
int32	g_CV_DelimitPackets = LTTRUE;

// GLOBAL: LITHTECH 0x004e36e0
int32	g_CV_MeasurePackets = LTFALSE; // Used to have the server build compression tables.


// GLOBAL: LITHTECH 0x004e36e4
char	*g_CV_BindIP = LTNULL;
// GLOBAL: LITHTECH 0x004e36e8
char	*g_CV_IP = LTNULL;

// GLOBAL: LITHTECH 0x004e36ec
int32	g_CV_SendBandwidth = 0;
// GLOBAL: LITHTECH 0x004e36f0
int32	g_CV_IPClientPort = 0;

// GLOBAL: LITHTECH 0x004e36f4
int32	g_CV_ForceConsole = 0;
// GLOBAL: LITHTECH 0x004e36f8
int32	g_CV_IPDevice = 0;
// GLOBAL: LITHTECH 0x004e36fc
int32	g_CV_IPDebug = LTFALSE;

// GLOBAL: LITHTECH 0x004d2140
float	g_CV_AckTimeout = 1.5f;
// GLOBAL: LITHTECH 0x004d2144
float	g_CV_IPQueryTimeout = 30.0f;
// GLOBAL: LITHTECH 0x004e3700
float	g_CV_MaxFPS = 0.0f;

// GLOBAL: LITHTECH 0x004e3704
int32	g_CV_InputDebug = LTFALSE;
// GLOBAL: LITHTECH 0x004e3708
int32	g_CV_JoystickDisable = LTFALSE;

// GLOBAL: LITHTECH 0x004d2148
int32	g_CV_UpdateRate = DEFAULT_CLIENT_UPDATE_RATE;

// GLOBAL: LITHTECH 0x004d214c
int32	g_CV_ConnTroubleCount = 50;
// GLOBAL: LITHTECH 0x004d2150
int32	g_CV_ConnTroubleCount2 = 70;
// GLOBAL: LITHTECH 0x004d2154
int32	g_CV_ConnTroubleCount3 = 50;
// GLOBAL: LITHTECH 0x004d2158
int32	g_CV_AllowTimeout = LTTRUE;

// GLOBAL: LITHTECH 0x004e370c
int32	g_CV_TraceConsole = LTFALSE;

// GLOBAL: LITHTECH 0x004e3710
int32	g_CV_ShowFileAccess = LTFALSE;

// GLOBAL: LITHTECH 0x004e3714
int32	g_CV_ShowConnStats = LTFALSE;

// GLOBAL: LITHTECH 0x004e3718
int32	g_CV_FullLightScale = LTFALSE;

// GLOBAL: LITHTECH 0x004e371c
int32	g_CV_ForceClear = LTFALSE;	// Force the engine to clear the screen.

// GLOBAL: LITHTECH 0x004d215c
int32	g_CV_MasterPaletteMode = 2;

// GLOBAL: LITHTECH 0x004d2160
int32	g_CV_ModelTransitionMS = 200;

// GLOBAL: LITHTECH 0x004d2164
int32	g_CV_NewCollision = LTTRUE;
// GLOBAL: LITHTECH 0x004d2168
int32	g_CV_NewStairStep = LTTRUE;

// GLOBAL: LITHTECH 0x004e3720
float	g_CV_LatencySim = 0.0f;	// Simulate latency.
// GLOBAL: LITHTECH 0x004e3724
float	g_CV_DropRate = 0.0f;   // Simulate packet drops - affects both client and server at the same time

// GLOBAL: LITHTECH 0x004e3728
int32	g_CV_InputRate=0;		// The amount of time an input sample is spread over in milliseconds


// GLOBAL: LITHTECH 0x004e372c
int32	g_bForceRemote = LTFALSE;	// Force remote connection even if it's local.. less efficient.
// GLOBAL: LITHTECH 0x004e3730
int32	g_bShowRunningTime = LTFALSE;	// Shows how long the engine has been running.
// GLOBAL: LITHTECH 0x004e3734
LTBOOL	g_bNullRender = LTFALSE;	// Set this when using NullRender.. the engine
									// will do things to make it work better.

// GLOBAL: LITHTECH 0x004e3738
LTBOOL	g_CV_HighPriority = LTFALSE;	// Should the process be set into high priority?

// Not in the table and not referenced: its name is unknown.
// GLOBAL: LITHTECH 0x004e373c
int32	g_CV_Unknown373c = 0;

// GLOBAL: LITHTECH 0x004e3740
int32	g_CV_ShowPing = LTFALSE;
// GLOBAL: LITHTECH 0x004e3744
int32	g_CV_ShowThruput = LTFALSE;

// GLOBAL: LITHTECH 0x004e3748
int32	g_bLocalDebug = LTFALSE;
// GLOBAL: LITHTECH 0x004e374c
int32	g_bPrediction = LTFALSE;
// GLOBAL: LITHTECH 0x004e3750
int32	g_nPredictionLines = 0; // Draw lines showing the results of client-side prediction
  								// 0 = no lines   1 = track prediction   >1 = stop
// GLOBAL: LITHTECH 0x004e3754
int32	g_TransportDebug = 0;
// GLOBAL: LITHTECH 0x004e3758
int32	g_CV_ParseNet_Incoming = 0; // Parse incoming network packets
// GLOBAL: LITHTECH 0x004e375c
int32	g_ClientSleepMS = 0;
// GLOBAL: LITHTECH 0x004e3760
int32	g_ShowTickCounts = 0;
// GLOBAL: LITHTECH 0x004e3764
int32	g_bShowMemStats = LTFALSE;
// GLOBAL: LITHTECH 0x004e3768
int32	g_bDebugStrings = LTFALSE;
// GLOBAL: LITHTECH 0x004e376c
int32	g_bDebugPackets = LTFALSE;
// GLOBAL: LITHTECH 0x004d216c
int32	g_CV_NetMaxQueue = 32;

// GLOBAL: LITHTECH 0x004d2170
float	g_CV_MaxExtrapolateTime = 0.5f;  // Maximum amount of time to extrapolate a position


// The console's global model light add / directional add / world model ambient colors
// (set by the ModelAdd, ModelDirAdd and WMAmbient commands), zero constructed here.
// FUNCTION: LITHTECH 0x00436100 _$E2
// FUNCTION: LITHTECH 0x00436110 _$E1
// GLOBAL: LITHTECH 0x004e36b4
LTVector g_ConsoleModelAdd(0.0f, 0.0f, 0.0f);
// FUNCTION: LITHTECH 0x00436130 _$E5
// FUNCTION: LITHTECH 0x00436140 _$E4
// GLOBAL: LITHTECH 0x004e369c
LTVector g_ConsoleModelDirAdd(0.0f, 0.0f, 0.0f);
// FUNCTION: LITHTECH 0x00436160 _$E8
// FUNCTION: LITHTECH 0x00436170 _$E7
// GLOBAL: LITHTECH 0x004e36a8
LTVector g_ConsoleModelDirAdd2(0.0f, 0.0f, 0.0f);
// GLOBAL: LITHTECH 0x004e3770
LTCommandVar	*g_pNameVar = LTNULL;
// GLOBAL: LITHTECH 0x004d2174
LTBOOL		g_bUpdateServer = LTTRUE;

// GLOBAL: LITHTECH 0x004d2178
float	g_CV_FarZ = 10000.0f;

// GLOBAL: LITHTECH 0x004e3774
LTBOOL	g_bSoundShowCounts = LTFALSE;
// GLOBAL: LITHTECH 0x004e3778
int32	g_nSoundDebugLevel = 0;

// GLOBAL: LITHTECH 0x004e377c
int32	g_bErrorLog = LTFALSE;
// GLOBAL: LITHTECH 0x004e3780
int32	g_bAlwaysFlushLog = LTFALSE;

// GLOBAL: LITHTECH 0x004d217c
int32	g_CV_BitDepth = 16;
// GLOBAL: LITHTECH 0x004e3784
int32	g_bSoftware = LTFALSE;
// GLOBAL: LITHTECH 0x004d2180
int32	g_ScreenWidth = 640;
// GLOBAL: LITHTECH 0x004d2184
int32	g_ScreenHeight = 480;
// GLOBAL: LITHTECH 0x004e3788
int32	g_CV_ShowFrameRate = LTFALSE;
// GLOBAL: LITHTECH 0x004d2188
int32	g_nConsoleLines = 6;
// GLOBAL: LITHTECH 0x004d218c
LTBOOL  g_bConsoleEnable = LTTRUE;
// GLOBAL: LITHTECH 0x004d2190
char	g_SSFile[100] = "Screenshot";

// GLOBAL: LITHTECH 0x004e378c
int32	g_bDoExtraObjectStuff = LTFALSE;

// GLOBAL: LITHTECH 0x004d21f4
LTBOOL	g_bMusicEnable = LTTRUE;
// GLOBAL: LITHTECH 0x004d21f8
LTBOOL	g_bSoundEnable = LTTRUE;
// GLOBAL: LITHTECH 0x004e3790
int32	g_CV_ForceSoundDisable = LTFALSE; // Force it to not initialize music or sound (behind the game's back).

// GLOBAL: LITHTECH 0x004e3794
LTBOOL	g_bAutoDeactivate = LTFALSE;

// GLOBAL: LITHTECH 0x004d21fc
float	g_fLodScale = 1.0f;

// Framerate for server logic.
// GLOBAL: LITHTECH 0x004d2200
float	g_ServerFPS = 30.0f;

// Scale the time to make things go slower or faster.
// GLOBAL: LITHTECH 0x004d2204
float	g_CV_TimeScale = 1.0f;

// If an object moves to a position with coord larger than this, it'll complain in the console.
// GLOBAL: LITHTECH 0x004e3798
float	g_DebugMaxPos = 0.0f;

// If they do a SetObjectDims larger than this, it'll complain in the console.
// GLOBAL: LITHTECH 0x004d2208
float	g_CV_DebugMaxDims = 3000.0f;

// GLOBAL: LITHTECH 0x004e379c
LTBOOL	g_CV_ShowGameTime = LTFALSE;

// GLOBAL: LITHTECH 0x004e37a0
LTBOOL	g_CV_ShowClassTicks = LTFALSE;

// GLOBAL: LITHTECH 0x004e37a4
LTBOOL	g_CV_ShowSphereFindTicks = LTFALSE;
// GLOBAL: LITHTECH 0x004e37a8
LTBOOL	g_CV_ShowPolyFindTicks = LTFALSE;

// Console attributes
// GLOBAL: LITHTECH 0x004d220c
int32	g_CV_ConsoleHistoryLen = 20;
// GLOBAL: LITHTECH 0x004d2210
int32	g_CV_ConsoleBufferLen = 500;
// GLOBAL: LITHTECH 0x004d2214
float	g_CV_ConsoleAlpha = 1.0f;		// Note : OptimizeSurfaces must be 1 for this to have any effect
// GLOBAL: LITHTECH 0x004d2218
int32	g_CV_ConsoleLeft = -1;	// Note : Negative values move the edge away from the screen edge by
// GLOBAL: LITHTECH 0x004d221c
int32	g_CV_ConsoleTop = -1;	//	that positive fraction of the screen dimension.  (-1 = no offset)
// GLOBAL: LITHTECH 0x004d2220
int32	g_CV_ConsoleRight = -1;
// GLOBAL: LITHTECH 0x004d2224
int32	g_CV_ConsoleBottom = -2;

// The LithTech DirectMusic console output level
// GLOBAL: LITHTECH 0x004d2228
int32	g_CV_LTDMConsoleOutput = 2;

//------------------------------------------------------------------
//------------------------------------------------------------------
// The main table of command variables
//------------------------------------------------------------------
//------------------------------------------------------------------

// GLOBAL: LITHTECH 0x004d2230
static LTEngineVar g_LTEngineVars[] =
{
	EV_STRING("BindIP", &g_CV_BindIP),
	EV_STRING("IP", &g_CV_IP),

	EV_LONG("FlashClients", &g_CV_FlashClients),
	EV_LONG("RenderEnable", &g_CV_RenderEnable),
	EV_LONG("PlayDemoReps", &g_CV_PlayDemoReps),
	EV_LONG("PlayDemoProfile", &g_CV_PlayDemoProfile),
	EV_LONG("ForceNoSound", &g_CV_ForceNoSound),
	EV_LONG("SoundEndCap", &g_bStopFilteredSamples),
	EV_LONG("SoundsPerBuffer", &g_dwMaxInstancesPerBuffer),

	EV_LONG("CursorCenter", &g_CV_CursorCenter),
	EV_LONG("CacheFiles", &g_CV_CacheFiles),

	EV_LONG("VideoDebug", &g_CV_VideoDebug),
	EV_LONG("DebugLoaders", &g_CV_DebugLoaders),
	EV_LONG("MeasurePackets", &g_CV_MeasurePackets),

	EV_LONG("STracePackets", &g_CV_STracePackets),
	EV_LONG("DelimitPackets", &g_CV_DelimitPackets),
	EV_LONG("SendBandwidth", &g_CV_SendBandwidth),
	EV_LONG("ForceConsole", &g_CV_ForceConsole),
	EV_LONG("IPDevice", &g_CV_IPDevice),
	EV_LONG("IPDebug", &g_CV_IPDebug),
	EV_LONG("IPClientPort", &g_CV_IPClientPort),
	EV_LONG("InputDebug", &g_CV_InputDebug),
	EV_LONG("UpdateRate", &g_CV_UpdateRate),
	EV_LONG("InputRate", &g_CV_InputRate),
	EV_LONG("ForceRemote", &g_bForceRemote),
	EV_LONG("NewCollision", &g_CV_NewCollision),
	EV_LONG("NewStairStep", &g_CV_NewStairStep),
	EV_LONG("DebugLevel", &g_DebugLevel),
	EV_LONG("ShowRunningTime", &g_bShowRunningTime),
	EV_LONG("NullRender", &g_bNullRender),
	EV_LONG("LocalDebug", &g_bLocalDebug),
	EV_LONG("TransportDebug", &g_TransportDebug),
	EV_LONG("ParseNet_Incoming", &g_CV_ParseNet_Incoming),
	EV_LONG("NetMaxQueue", &g_CV_NetMaxQueue),
	EV_LONG("ClientSleepMS", &g_ClientSleepMS),
	EV_LONG("ShowTickCounts", &g_ShowTickCounts),
	EV_LONG("ShowMemStats", &g_bShowMemStats),
	EV_LONG("Prediction", &g_bPrediction),
	EV_LONG("PredictionLines", &g_nPredictionLines),
	EV_LONG("DebugStrings", &g_bDebugStrings),
	EV_LONG("DebugPackets", &g_bDebugPackets),
	EV_LONG("DoExtraObjectStuff", &g_bDoExtraObjectStuff),
	EV_LONG("DebugNumSounds", &g_bSoundShowCounts),
	EV_LONG("DebugSound", &g_nSoundDebugLevel),
	EV_LONG("ErrorLog", &g_bErrorLog),
	EV_LONG("AlwaysFlushLog", &g_bAlwaysFlushLog),
	EV_LONG("ShowFrameRate", &g_CV_ShowFrameRate),
	EV_LONG("NumConsoleLines", &g_nConsoleLines),
	EV_LONG("ConsoleEnable", &g_bConsoleEnable),
	EV_LONG("ScreenWidth", &g_ScreenWidth),
	EV_LONG("ScreenHeight", &g_ScreenHeight),
	EV_LONG("Software", &g_bSoftware),
	EV_LONG("BitDepth", &g_CV_BitDepth),
	EV_LONG("MusicEnable", &g_bMusicEnable),
	EV_LONG("SoundEnable", &g_bSoundEnable),
	EV_LONG("ForceSoundDisable", &g_CV_ForceSoundDisable),
	EV_LONG("AllowTimeout", &g_CV_AllowTimeout),
	EV_LONG("ShowSphereFindTicks", &g_CV_ShowSphereFindTicks),
	EV_LONG("ShowPolyFindTicks", &g_CV_ShowPolyFindTicks),
	EV_LONG("ShowClassTicks", &g_CV_ShowClassTicks),
	EV_LONG("ShowGameTime", &g_CV_ShowGameTime),
	EV_LONG("AutoDeactivate", &g_bAutoDeactivate),
	EV_LONG("JoystickDisable", &g_CV_JoystickDisable),
	EV_LONG("ConnTroubleCount", &g_CV_ConnTroubleCount),
	EV_LONG("ConnTroubleCount2", &g_CV_ConnTroubleCount2),
	EV_LONG("ConnTroubleCount3", &g_CV_ConnTroubleCount3),
	EV_LONG("TraceConsole", &g_CV_TraceConsole),
	EV_LONG("ShowFileAccess", &g_CV_ShowFileAccess),
	EV_LONG("FullLightScale", &g_CV_FullLightScale),
	EV_LONG("ForceClear", &g_CV_ForceClear),
	EV_LONG("ModelTransitionMS", &g_CV_ModelTransitionMS),
	EV_LONG("HighPriority", &g_CV_HighPriority),
	EV_LONG("ShowConnStats", &g_CV_ShowConnStats),
	EV_LONG("ShowPing", &g_CV_ShowPing),
	EV_LONG("ShowThruput", &g_CV_ShowThruput),
	EV_LONG("MasterPaletteMode", &g_CV_MasterPaletteMode),

	EV_LONG("LTDMConsoleOutput", &g_CV_LTDMConsoleOutput),

	EV_CVAR("Name", &g_pNameVar),

	EV_FLOAT("DefaultDrawIndexedDist", &g_CV_DefaultDrawIndexedDist),
	EV_FLOAT("MaxFPS", &g_CV_MaxFPS),
	EV_FLOAT("LatencySim", &g_CV_LatencySim),
	EV_FLOAT("DropRate", &g_CV_DropRate),
	EV_FLOAT("FarZ", &g_CV_FarZ),
	EV_FLOAT("LODScale", &g_fLodScale),
	EV_FLOAT("DebugMaxDims", &g_CV_DebugMaxDims),
	EV_FLOAT("DebugMaxPos", &g_DebugMaxPos),
	EV_FLOAT("TimeScale", &g_CV_TimeScale),
	EV_FLOAT("AckTimeout", &g_CV_AckTimeout),
	EV_FLOAT("MaxExtrapolateTime", &g_CV_MaxExtrapolateTime),

	EV_LONG("ConsoleHistoryLen", &g_CV_ConsoleHistoryLen),
	EV_LONG("ConsoleBufferLen", &g_CV_ConsoleBufferLen),
	EV_FLOAT("ConsoleAlpha", &g_CV_ConsoleAlpha),
	EV_LONG("ConsoleLeft", &g_CV_ConsoleLeft),
	EV_LONG("ConsoleTop", &g_CV_ConsoleTop),
	EV_LONG("ConsoleRight", &g_CV_ConsoleRight),
	EV_LONG("ConsoleBottom", &g_CV_ConsoleBottom),
};

// Functions that other modules call.
// FUNCTION: LITHTECH 0x00436190
LTEngineVar* GetEngineVars() {return g_LTEngineVars;}
// FUNCTION: LITHTECH 0x004361a0
int GetNumEngineVars() {return sizeof(g_LTEngineVars) / sizeof(LTEngineVar);}
