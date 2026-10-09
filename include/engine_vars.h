// Console variables and engine-wide settings, defined in engine_vars.cpp (g_LTEngineVars points at them).
#ifndef __ENGINE_VARS_H__
#define __ENGINE_VARS_H__

#include "ltbasedefs.h"

struct LTCommandVar;


extern int32 g_CV_RenderEnable;
extern int32 g_CV_FlashClients;
extern int32 g_CV_PlayDemoReps;
extern int32 g_CV_PlayDemoProfile;
extern int32 g_CV_ForceNoSound;
// The ForceRemote console variable (the engine forces local clients to be remote).
extern int32 g_bForceRemote;
extern LTBOOL g_bStopFilteredSamples;
extern uint32 g_dwMaxInstancesPerBuffer;
extern int32 g_CV_CursorCenter;
extern LTBOOL g_CV_CacheFiles;
extern int32 g_CV_VideoDebug;
extern int32 g_CV_DebugLoaders;
extern int32 g_CV_STracePackets;
extern int32 g_CV_DelimitPackets;
extern int32 g_CV_MeasurePackets;
extern char *g_CV_BindIP;
extern char *g_CV_IP;
extern int32 g_CV_SendBandwidth;
extern int32 g_CV_IPClientPort;
extern int32 g_CV_ForceConsole;
extern int32 g_CV_IPDevice;
extern int32 g_CV_IPDebug;
extern float g_CV_AckTimeout;
extern float g_CV_IPQueryTimeout;
extern float g_CV_MaxFPS;
extern int32 g_CV_InputDebug;
extern int32 g_CV_JoystickDisable;
extern int32 g_CV_UpdateRate;
extern int32 g_CV_ConnTroubleCount;
extern int32 g_CV_ConnTroubleCount2;
extern int32 g_CV_ConnTroubleCount3;
extern int32 g_CV_AllowTimeout;
extern int32 g_CV_TraceConsole;
extern int32 g_CV_ShowFileAccess;
extern int32 g_CV_ShowConnStats;
extern int32 g_CV_FullLightScale;
extern int32 g_CV_ForceClear;
extern int32 g_CV_MasterPaletteMode;
extern int32 g_CV_ModelTransitionMS;
extern float g_CV_LatencySim;
extern float g_CV_DropRate;
extern int32 g_CV_InputRate;
extern int32 g_bShowRunningTime;
extern LTBOOL g_bNullRender;
extern LTBOOL g_CV_HighPriority;
extern int32 g_CV_Unknown373c;
extern int32 g_CV_ShowPing;
extern int32 g_CV_ShowThruput;
extern int32 g_bLocalDebug;
extern int32 g_bPrediction;
extern int32 g_nPredictionLines;
extern int32 g_TransportDebug;
extern int32 g_CV_ParseNet_Incoming;
extern int32 g_ClientSleepMS;
extern int32 g_ShowTickCounts;
extern int32 g_bShowMemStats;
extern int32 g_bDebugStrings;
extern int32 g_bDebugPackets;
extern int32 g_CV_NetMaxQueue;
extern float g_CV_MaxExtrapolateTime;
extern LTVector g_ConsoleModelAdd;
extern LTVector g_ConsoleModelDirAdd;
extern LTVector g_ConsoleModelDirAdd2;
extern LTCommandVar *g_pNameVar;
extern LTBOOL g_bUpdateServer;
extern float g_CV_FarZ;
extern LTBOOL g_bSoundShowCounts;
extern int32 g_nSoundDebugLevel;
extern int32 g_bErrorLog;
extern int32 g_bAlwaysFlushLog;
extern int32 g_CV_BitDepth;
extern int32 g_bSoftware;
extern int32 g_ScreenWidth;
extern int32 g_ScreenHeight;
extern int32 g_CV_ShowFrameRate;
extern int32 g_nConsoleLines;
extern LTBOOL g_bConsoleEnable;
extern char g_SSFile[100];
extern int32 g_bDoExtraObjectStuff;
extern LTBOOL g_bMusicEnable;
extern LTBOOL g_bSoundEnable;
extern int32 g_CV_ForceSoundDisable;
extern float g_fLodScale;
extern int32 g_CV_ConsoleHistoryLen;
extern int32 g_CV_ConsoleBufferLen;
extern float g_CV_ConsoleAlpha;
extern int32 g_CV_ConsoleLeft;
extern int32 g_CV_ConsoleTop;
extern int32 g_CV_ConsoleRight;
extern int32 g_CV_ConsoleBottom;
extern int32 g_CV_LTDMConsoleOutput;

#endif  // __ENGINE_VARS_H__
