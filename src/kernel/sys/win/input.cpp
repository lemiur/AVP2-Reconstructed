// Jupiter runtime/kernel/src/sys/win/input.cpp
// Talon is a DirectInput 7 build (DirectInputCreate with 0x700, falling back to 0x300; devices
// are queried for IDirectInputDevice2). There are no gamepads, AddTrigger searches the device
// objects through file statics instead of a context struct, and the input manager has no
// GetDeviceObjectName.
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#define DIRECTINPUT_VERSION 0x0700
#include "../../jupiter/dx9inc/dinput.h"
#include "bdefs.h"
#include "concommand.h"
#include "dsys_interface.h"
#include "input.h"
#include "../../build/proj/LT2/lithshared/stdlith/goodlinklist.h"
#include "../../build/proj/LT2/lithshared/stdlith/helpers.h"
#include "engine_vars.h"

/*

	LithTech supports:

		3 axis -- x,y (mouse/joystick), and z
		toggle buttons - trigger actions
		push buttons - trigger actions

		Now, an actioncode of -10000 or less uses a console variable with the trigger name!
*/



class CJoystickEffect
{
	public:

		TCHAR					m_EffectName[256];	// 0x000
		LPDIRECTINPUTEFFECT		m_pEffect;			// 0x100

		CJoystickEffect*		m_pNext;			// 0x104
};


enum InputType
{
	Input_PushButton=0,
	Input_ToggleButton=1,
	Input_AbsAxis=2,
	Input_RelAxis=3,
	Input_Pov=4
};


struct TrackObjectInfo
{
	GUID		guidType;			// 0x00
	uint32		dwOfs;				// 0x10
	uint32		dwType;				// 0x14
	TCHAR		tszName[MAX_PATH];	// 0x18
};

#define MAX_OBJECT_BINDINGS	150
#define INPUT_BUFFER_SIZE	64

// --------------------------------------------------------------------- //
// Action definitions
// --------------------------------------------------------------------- //

class ActionDef
{
	public:

				ActionDef()
				{
					m_pPrev = m_pNext = this;
				}

		int		m_ActionCode;						// 0x00
		char	m_ActionName[MAX_ACTIONNAME_LEN];	// 0x04

		ActionDef	*m_pPrev, *m_pNext;				// 0x24, 0x28

};

// FUNCTION: LITHTECH 0x0043eb70 _$E2
// FUNCTION: LITHTECH 0x0043eb80 _$E1
// GLOBAL: LITHTECH 0x004e3990
ActionDef g_ActionDefHead;


// GLOBAL: LITHTECH 0x004e3b0c
static ConsoleState		*g_pInputConsoleState = LTNULL;


// --------------------------------------------------------------------- //
// Device-to-action mappings
// --------------------------------------------------------------------- //

class CTriggerActionConsoleVar;

// list of all the console variables that are used in the TriggerAction
// GLOBAL: LITHTECH 0x004e3b10
static CTriggerActionConsoleVar*	g_lstTriggerActionConsoleVariables = LTNULL;

class TriggerAction
{
	public:

						TriggerAction()
						{
							m_pConsoleString = LTNULL;
							m_pAction = LTNULL;
							m_pNext = LTNULL;
							m_pConsoleVar = LTNULL;
						}

						inline ~TriggerAction();

		// Both zero if it's not using range.
		float			m_RangeLow, m_RangeHigh;	// 0x00, 0x04

		// If this is non-LTNULL, then the action is a console string instead of an ActionDef.
		char			*m_pConsoleString;			// 0x08

		ActionDef		*m_pAction;					// 0x0c
		TriggerAction	*m_pNext;					// 0x10

		// Contains a pointer to a trigger console var structure that needs to be updated
		// this currently only works for axis types
		// Set to LTNULL if there is no console variable to update.
		CTriggerActionConsoleVar*	m_pConsoleVar;	// 0x14

};

class DeviceDef;
class TriggerObject : public CGLLNode
{
	public:

						TriggerObject()
						{
							m_State = 0.0f;
							m_PrevState = 0.0f;
							m_BaseState = 0.0f;
							m_bJustWentDown = TRUE;
							m_dwUpdateTime = 0;
							m_dwPrevUpdateTime = 0;
							m_dwBaseUpdateTime = 0;
							m_Scale = 1.0f;
						}

						~TriggerObject()
						{
							TriggerAction	*pCur, *pNext;

							pCur = m_pActionHead;
							while( pCur )
							{
								pNext = pCur->m_pNext;
								delete pCur;
								pCur = pNext;
							}
						}

		// Info on the trigger (key, mouse axis, or mouse button...)
		TCHAR			m_TriggerName[INPUTNAME_LEN];	// 0x008
		TCHAR			m_RealName[INPUTNAME_LEN];		// 0x06c Alternate name for it (like if m_TriggerName is ##21, this could be 'Space bar').
		uint32			m_diType;			// 0x0d0
		GUID			m_diGuid;			// 0x0d4
		InputType		m_InputType;		// 0x0e4 What DirectEngine sees it as.
		uint32			m_StateIndex;		// 0x0e8 index in the object state data array

		float			m_State;			// 0x0ec The current state of the trigger
		float			m_PrevState;		// 0x0f0 Previous state of the trigger
		float			m_BaseState;		// 0x0f4

		uint32			m_dwUpdateTime;		// 0x0f8 The time of the most recent update.
		uint32			m_dwPrevUpdateTime;	// 0x0fc The time of the previous update
		uint32			m_dwBaseUpdateTime;	// 0x100

		LTBOOL			m_bJustWentDown;	// 0x104

		// If it's a relative axis trigger, this scales the value.
		float			m_Scale;			// 0x108

		// scale an axis value to a range if these are not both 0.0
		float			m_fRangeScaleMin;				// 0x10c
		float			m_fRangeScaleMax;				// 0x110
		float			m_fRangeScaleMultiplier;		// 0x114
		float			m_fRangeScaleOffset;			// 0x118
		float			m_fRangeScaleMultiplierLo;		// 0x11c
		float			m_fRangeScaleOffsetLo;			// 0x120
		float			m_fRangeScaleMultiplierHi;		// 0x124
		float			m_fRangeScaleOffsetHi;			// 0x128
		float			m_fRangeScalePreCenterOffset;	// 0x12c
		float			m_fRangeScalePreCenter;			// 0x130

		// Which device it comes from.
		DeviceDef		*m_pDevice;			// 0x134

		// The actions it will trigger.
		TriggerAction	*m_pActionHead;		// 0x138

		// Data range.
		float		m_DataMin;				// 0x13c
		float		m_DataMax;				// 0x140

};


// Trigger objects that have been created.
// FUNCTION: LITHTECH 0x0043eb90 _$E7
// FUNCTION: LITHTECH 0x0043eba0 _$E4
// FUNCTION: LITHTECH 0x0043ebf0 _$E6
// FUNCTION: LITHTECH 0x0043ec00 _$E5
// GLOBAL: LITHTECH 0x004e3838
static TriggerObject	g_TriggerHead;

// class that holds records for storing trigger actions in a console variable
class CTriggerActionConsoleVar
{
	public:
					CTriggerActionConsoleVar()
					{
						m_pTriggerAction = LTNULL;
						m_pCommandVar = LTNULL;
					}

					~CTriggerActionConsoleVar()
					{
						m_pTriggerAction = LTNULL;
						m_pCommandVar = LTNULL;

						// remove from global list
						if (g_lstTriggerActionConsoleVariables == this)
						{
							g_lstTriggerActionConsoleVariables = this->m_pNext;
						}
						else
						{
							CTriggerActionConsoleVar* pConVar = g_lstTriggerActionConsoleVariables;
							while (pConVar != LTNULL)
							{
								if (pConVar->m_pNext == this)
								{
									pConVar->m_pNext = this->m_pNext;
									pConVar = LTNULL;
								}
								else pConVar = pConVar->m_pNext;
							}
						}
					}

					void InitCommandVar()
					{
						m_pCommandVar = cc_FindConsoleVar(g_pInputConsoleState, GetCommandName());
					}

					char* GetCommandName()
					{
						if (m_pTriggerAction == LTNULL) return LTNULL;
						return m_pTriggerAction->m_pAction->m_ActionName;
					}

					void SetFloat(float nVal)
					{
						char sConValue[100];

						if (m_pTriggerAction != LTNULL)
						{
							_snprintf(sConValue, sizeof(sConValue), "%f", nVal);
							sConValue[sizeof(sConValue)-1] = 0;
							cc_SetConsoleVariable(g_pInputConsoleState, GetCommandName(), sConValue);
						}
					}

					float GetFloat()
					{
						if (m_pCommandVar == LTNULL) return 0.0;
						else return m_pCommandVar->floatVal;
					}

		// trigger action that contains this console var
		TriggerAction*				m_pTriggerAction;	// 0x00

		// pointer to data structure that holds information about the console variable
		LTCommandVar*				m_pCommandVar;		// 0x04

		// pointer to next item in the list
		CTriggerActionConsoleVar*	m_pNext;			// 0x08
};

// FUNCTION: LITHTECH 0x0043ee10 ??_GCTriggerActionConsoleVar@@QAEPAXI@Z


class DeviceDef
{
	public:

					DeviceDef()
					{
						m_pDevice = LTNULL;
						m_bTracking = FALSE;
						m_bTrackingOnly = FALSE;
						m_dwLastTime = GetTickCount( );
						m_bSysMouse = FALSE;
						m_pTrackObjects = LTNULL;
						m_nTrackObjects = 0;
					}

					~DeviceDef()
					{
						GPOS pos;

						for(pos=m_Triggers; pos; )
						{
							delete m_Triggers.GetNext(pos);
						}

						m_Triggers.RemoveAll();
					}

		LTBOOL		IsEnabled()		{ return !!m_pDevice; }
		LTBOOL		IsJoystick()	{ return GET_DIDEVICE_TYPE(m_DeviceType) == DIDEVTYPE_JOYSTICK; }

		// LTNULL if this device hasn't been enabled.
		LPDIRECTINPUTDEVICE2	m_pDevice;			// 0x000

		// If it's referenced by one of the special names (##mouse or ##keyboard),
		// then it's stored here and should be saved in the bindings as such.
		char		*m_pSpecialName;				// 0x004

		// How many bytes used to read a state (GetDeviceState()).
		uint32		m_StateReadSize;				// 0x008

		TCHAR		m_InstanceName[INPUTNAME_LEN];	// 0x00c
		GUID		m_InstanceGuid;					// 0x070
		uint32		m_DeviceType;					// 0x080

		CGLinkedList<TriggerObject*>	m_Triggers;	// 0x084

		// Trigger object pointers (indexed by device data offset / 4)
		TriggerObject*	m_TriggerTable[MAX_OBJECT_BINDINGS];	// 0x090

		// Are we tracking this device?
		LTBOOL		m_bTracking;					// 0x2e8

		// Is this device enabled solely for tracking purposes?
		LTBOOL		m_bTrackingOnly;				// 0x2ec

		// Info on all objects for this device
		TrackObjectInfo*	m_pTrackObjects;		// 0x2f0
		uint32				m_nTrackObjects;		// 0x2f4

		uint32		m_dwLastTime;					// 0x2f8

		LTBOOL			m_bSysMouse;				// 0x2fc
		DeviceDef		*m_pNext;					// 0x300

};

// DirectInput stuff.

	// The window input is attached to.
	// GLOBAL: LITHTECH 0x004e3b14
	static HWND				g_InputHWND=LTNULL;

	// The main DirectInput object.
	// GLOBAL: LITHTECH 0x004e3b18
	static LPDIRECTINPUT	g_pDirectInput=LTNULL;

	// Devices we've enumerated.
	// GLOBAL: LITHTECH 0x004e3b1c
	static DeviceDef		*g_pDeviceHead=LTNULL;

	// Force-Feedback effects
	// GLOBAL: LITHTECH 0x004e3b20
	static CJoystickEffect	*g_pJoystickEffects=LTNULL;

	// Useful for remembering current device when enumerating device objects
	// GLOBAL: LITHTECH 0x004e3b24
	static DeviceDef		*g_pCurrentEnumDevice=LTNULL;

	// What input_AddTrigger is looking for (DeviceObjectEnumCallback).
	// GLOBAL: LITHTECH 0x004e39c4
	static LTBOOL			g_bLookingForSpecial;
	// GLOBAL: LITHTECH 0x004e39bc
	static uint32			g_SpecialType;
	// GLOBAL: LITHTECH 0x004e397c
	static uint32			g_nSpecialOffset;
	// GLOBAL: LITHTECH 0x004e3980
	static GUID				g_SpecialGuid;
	// GLOBAL: LITHTECH 0x004e39c8
	static LTBOOL			g_bSpecialGuid;
	// GLOBAL: LITHTECH 0x004e39c0
	static LTBOOL			g_bFoundObject;
	// GLOBAL: LITHTECH 0x004e39d0
	static DIDEVICEOBJECTINSTANCE	g_DeviceEnumFindings;



TriggerAction::~TriggerAction()
{
	if(m_pConsoleString)
		delete m_pConsoleString;

	// remove the console var
	if (m_pConsoleVar != LTNULL)
	{
		delete m_pConsoleVar;
		m_pConsoleVar = LTNULL;
	}
}

// --------------------------------------------------------------------- //
// Helpers.
// --------------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x004401e0
static ActionDef* input_FindAction( const char *pActionName )
{
	ActionDef	*pCur;

	pCur = g_ActionDefHead.m_pNext;
	while(pCur != &g_ActionDefHead)
	{
		if( CHelpers::UpperStrcmp(pActionName, pCur->m_ActionName) )
			return pCur;

		pCur = pCur->m_pNext;
	}

	return LTNULL;
}


// FUNCTION: LITHTECH 0x0043f430
static DeviceDef* input_FindDeviceByType( uint32 nType )
{
	// Go through the first time looking for non-HID versions (on Win9x there are two types)...
	DeviceDef* pDev = g_pDeviceHead;
	while (pDev) {
		if (GET_DIDEVICE_TYPE(pDev->m_DeviceType) == nType) {
			// Make sure it's not HID (usb) ... even if it is HID,
			// there will be a non-HID version as well and we need to use that one.
			if (!(pDev->m_DeviceType & DIDEVTYPE_HID)) {
				return pDev; } }
		pDev = pDev->m_pNext; }

 	// Do second pass for device, checking only HID devices.  This is a Win2K fix as all devices
 	//	on Win2K will be HID devices due to Win2ks plug and play manager.
 	//	This may also fix some Win9x issues as well.
 	pDev = g_pDeviceHead;
 	while (pDev) {
 		if (GET_DIDEVICE_TYPE( pDev->m_DeviceType ) == nType) {
 			// check for HID devices only... if we had a non-HID device, we would not have gotten here
 			if ((pDev->m_DeviceType & DIDEVTYPE_HID)) {
				return pDev; } }
  		pDev = pDev->m_pNext; }

	return LTNULL;
}

// FUNCTION: LITHTECH 0x00441d20
static DeviceDef* FindDeviceByTypeWithSkip( uint32 nType, int nSkipNum = 0 )
{
	DeviceDef	*pDev;

	pDev = g_pDeviceHead;
	while( pDev )
	{
		if( GET_DIDEVICE_TYPE( pDev->m_DeviceType ) == nType )
		{
			if (nSkipNum <= 0) return pDev;
			else nSkipNum--;
		}

		pDev = pDev->m_pNext;
	}

	return LTNULL;
}


// FUNCTION: LITHTECH 0x0043f370
static DeviceDef* input_FindDeviceByName( const TCHAR *pName )
{
	DeviceDef *pDev;

	// Look for it with the normal name.
	pDev = g_pDeviceHead;
	while( pDev )
	{
		if( CHelpers::UpperStrcmp(pName, pDev->m_InstanceName) )
			return pDev;

		pDev = pDev->m_pNext;
	}

	// Look for it by its special name.
	if(stricmp(pName, "##mouse") == 0)
	{
		pDev = input_FindDeviceByType(DIDEVTYPE_MOUSE);
		if(pDev)
			pDev->m_pSpecialName = "##mouse";

		return pDev;
	}
	else if(stricmp(pName, "##keyboard") == 0)
	{
		pDev = input_FindDeviceByType(DIDEVTYPE_KEYBOARD);
		if(pDev)
			pDev->m_pSpecialName = "##keyboard";
		return pDev;
	}
	// KEF - 1/4/00 - Evil hack to handle Win98 SE not defining "Joystick 1"
	else if(strnicmp(pName, "Joystick 1", 8) == 0)
	{
		pDev = input_FindDeviceByType(DIDEVTYPE_JOYSTICK);
		if(pDev)
			pDev->m_pSpecialName = "Joystick 1";

		return pDev;
	}

	return LTNULL;
}


// FUNCTION: LITHTECH 0x00440360
static TriggerObject* input_FindTrigger(DeviceDef *pDef, const char *pName)
{
	TriggerObject *pCur;
	GPOS pos;

	// Look for it the normal way..
	for(pos=pDef->m_Triggers; pos; )
	{
		pCur = pDef->m_Triggers.GetNext(pos);

		if(stricmp(pCur->m_TriggerName, pName)==0 || stricmp(pCur->m_RealName, pName)==0)
			return pCur;
	}

	return LTNULL;
}

// Forward declarations
LTRESULT input_GetManager(InputMgr **pMgr);


// FUNCTION: LITHTECH 0x00440ce0
static BOOL CALLBACK DeviceObjectEnumCallback( LPCDIDEVICEOBJECTINSTANCE pObj, LPVOID pvRef )
{
	if( g_bLookingForSpecial )
	{
		if( g_bSpecialGuid )
		{
			if( pObj->guidType == g_SpecialGuid )
			{
				// Make sure we have the right offset.
				if( g_nSpecialOffset != 0xFFFFFFFF)
				{
					if(pObj->dwOfs != g_nSpecialOffset )
					{
						return DIENUM_CONTINUE;
					}
				}

				g_bFoundObject = TRUE;
				memcpy( &g_DeviceEnumFindings, pObj, sizeof(DIDEVICEOBJECTINSTANCE) );
				return DIENUM_STOP;
			}
		}
		else if( g_SpecialType != 0xFFFFFFFF)
		{
			if(DIDFT_GETINSTANCE(pObj->dwType) == g_SpecialType )
			{
				g_bFoundObject = TRUE;
				memcpy( &g_DeviceEnumFindings, pObj, sizeof(DIDEVICEOBJECTINSTANCE) );
				return DIENUM_STOP;
			}
		}
		else if( g_nSpecialOffset != 0xFFFFFFFF)
		{
			if( pObj->dwOfs == g_nSpecialOffset )
			{
				g_bFoundObject = TRUE;
				memcpy( &g_DeviceEnumFindings, pObj, sizeof(DIDEVICEOBJECTINSTANCE) );
				return DIENUM_STOP;
			}
		}
	}
	else
	{
		if(stricmp( pObj->tszName, (const char*)pvRef ) == 0)
		{
			g_bFoundObject = TRUE;
			memcpy( &g_DeviceEnumFindings, pObj, sizeof(DIDEVICEOBJECTINSTANCE) );
			return DIENUM_STOP;
		}
	}

	return DIENUM_CONTINUE;
}


// FUNCTION: LITHTECH 0x00440650
static TriggerObject* input_AddTrigger(DeviceDef *pDevice, const char *pTriggerName)
{
	HRESULT hResult;
	TriggerObject *pTrigger;
	int inputType;
	DIPROPRANGE dipr;
	LPDIRECTINPUTDEVICE2 pDIDevice;


	g_bLookingForSpecial = FALSE;

	// Setup for special types..
	if(pTriggerName[0] != 0 && pTriggerName[1] != 0)
	{
		if(pTriggerName[0] == '#' && pTriggerName[1] == '#')
		{
			g_bLookingForSpecial = TRUE;
			g_SpecialType = g_nSpecialOffset = 0xFFFFFFFF;
			g_bSpecialGuid = FALSE;

			if(stricmp(&pTriggerName[2], "x-axis") == 0)
			{
				g_bSpecialGuid = TRUE;
				g_SpecialGuid = GUID_XAxis;
			}
			else if(stricmp(&pTriggerName[2], "y-axis") == 0)
			{
				g_bSpecialGuid = TRUE;
				g_SpecialGuid = GUID_YAxis;
			}
			else if(stricmp(&pTriggerName[2], "z-axis") == 0)
			{
				g_bSpecialGuid = TRUE;
				g_SpecialGuid = GUID_ZAxis;
			}
			else if(stricmp(&pTriggerName[2], "Button 0") == 0)
			{
				g_bSpecialGuid = TRUE;
				g_SpecialGuid = GUID_Button;
				g_nSpecialOffset = DIMOFS_BUTTON0;
			}
			else if(stricmp(&pTriggerName[2], "Button 1") == 0)
			{
				g_bSpecialGuid = TRUE;
				g_SpecialGuid = GUID_Button;
				g_nSpecialOffset = DIMOFS_BUTTON1;
			}
			else if(stricmp(&pTriggerName[2], "Button 2") == 0)
			{
				g_bSpecialGuid = TRUE;
				g_SpecialGuid = GUID_Button;
				g_nSpecialOffset = DIMOFS_BUTTON2;
			}
			else if(stricmp(&pTriggerName[2], "Slider 0") == 0)
			{
				g_bSpecialGuid = TRUE;
				g_SpecialGuid = GUID_Slider;
				g_nSpecialOffset = DIJOFS_SLIDER(0);
			}
			else if(stricmp(&pTriggerName[2], "Slider 1") == 0)
			{
				g_bSpecialGuid = TRUE;
				g_SpecialGuid = GUID_Slider;
				g_nSpecialOffset = DIJOFS_SLIDER(1);
			}
			else if(stricmp(&pTriggerName[2], "POV 0") == 0)
			{
				g_bSpecialGuid = TRUE;
				g_SpecialGuid = GUID_POV;
				g_nSpecialOffset = DIJOFS_POV(0);
			}
			else if(stricmp(&pTriggerName[2], "POV 1") == 0)
			{
				g_bSpecialGuid = TRUE;
				g_SpecialGuid = GUID_POV;
				g_nSpecialOffset = DIJOFS_POV(1);
			}
			else if(stricmp(&pTriggerName[2], "POV 2") == 0)
			{
				g_bSpecialGuid = TRUE;
				g_SpecialGuid = GUID_POV;
				g_nSpecialOffset = DIJOFS_POV(2);
			}
			else if(stricmp(&pTriggerName[2], "POV 3") == 0)
			{
				g_bSpecialGuid = TRUE;
				g_SpecialGuid = GUID_POV;
				g_nSpecialOffset = DIJOFS_POV(3);
			}
			else if(isalnum(pTriggerName[2]))
			{
				g_SpecialType = (uint32)atoi(&pTriggerName[2]);
			}
		}
	}

	// Enumerate objects on this device.
	g_bFoundObject = FALSE;
	hResult = pDevice->m_pDevice->EnumObjects( DeviceObjectEnumCallback, (void*)pTriggerName, DIDFT_ALL );
	if( hResult != DI_OK || !g_bFoundObject )
		return LTNULL;

	// Found an object with that name.  Make sure DirectEngine supports its type of input.
	if( g_DeviceEnumFindings.dwType & DIDFT_PSHBUTTON )
		inputType = Input_PushButton;
	else if( g_DeviceEnumFindings.dwType & DIDFT_TGLBUTTON )
		inputType = Input_ToggleButton;
	else if( g_DeviceEnumFindings.dwType & DIDFT_RELAXIS )
		inputType = Input_RelAxis;
	else if( g_DeviceEnumFindings.dwType & DIDFT_ABSAXIS )
		inputType = Input_AbsAxis;
	else if( g_DeviceEnumFindings.dwType & DIDFT_POV )
		inputType = Input_Pov;
	else
		return LTNULL;

	// Setup a TriggerObject for it.
	pTrigger = new TriggerObject;
	memset( pTrigger, 0, sizeof(TriggerObject) );

	pDevice->m_Triggers.AddHead(pTrigger);

	strncpy(pTrigger->m_RealName, pTriggerName, INPUTNAME_LEN);
	strncpy(pTrigger->m_TriggerName, g_DeviceEnumFindings.tszName, INPUTNAME_LEN);

	pTrigger->m_InputType = (InputType)inputType;
	memcpy( &pTrigger->m_diGuid, &g_DeviceEnumFindings.guidType, sizeof(GUID) );

	pTrigger->m_diType = g_DeviceEnumFindings.dwType;

	pTrigger->m_pDevice = pDevice;
	pTrigger->m_Scale = 1.0f;
	pTrigger->m_fRangeScaleMin = 0.0f;
	pTrigger->m_fRangeScaleMax = 0.0f;
	pTrigger->m_fRangeScaleMultiplier = 1.0f;
	pTrigger->m_fRangeScaleOffset = 0.0f;
	pTrigger->m_fRangeScaleMultiplierHi = 1.0f;
	pTrigger->m_fRangeScaleOffsetHi = 0.0f;
	pTrigger->m_fRangeScaleMultiplierLo = 1.0f;
	pTrigger->m_fRangeScaleOffsetLo = 0.0f;
	pTrigger->m_fRangeScalePreCenterOffset = 0.0f;
	pTrigger->m_fRangeScalePreCenter = 0.0f;

	// Get the trigger max range.
	pTrigger->m_DataMin = ( float )DIPROPRANGE_NOMIN;
	pTrigger->m_DataMax = ( float )DIPROPRANGE_NOMAX;
	pDIDevice = pDevice->m_pDevice;
	if (pDIDevice && ( inputType == Input_AbsAxis || inputType == Input_Pov ))
	{
		dipr.diph.dwSize = sizeof( DIPROPRANGE );
		dipr.diph.dwHeaderSize = sizeof( DIPROPHEADER );
		dipr.diph.dwObj = g_DeviceEnumFindings.dwType;
		dipr.diph.dwHow = DIPH_BYID;
		hResult = pDIDevice->GetProperty( DIPROP_RANGE, &dipr.diph );
		if( hResult == DI_OK )
		{
			pTrigger->m_DataMin = ( float )dipr.lMin;
			pTrigger->m_DataMax = ( float )dipr.lMax;
		}
	}

	return pTrigger;
}



// --------------------------------------------------------------------- //
// The main function that sets up how we'll communicate with DirectInput
// about the device objects.
// --------------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x0043fe60
static LTBOOL input_SetupDeviceFormats( DeviceDef *pDevice )
{
	DIOBJECTDATAFORMAT objectFormats[MAX_OBJECT_BINDINGS];
	DIDATAFORMAT format;
	DIPROPDWORD prop;
	TriggerObject *pTrigger;
	int iTrigger;
	HRESULT hResult;
	uint32 curOffset;
	uint32 dwStates[MAX_OBJECT_BINDINGS];
	LTBOOL bDuplicate;
	TriggerObject *ptr;
	GPOS pos, pos2;


	// Unacquire it.
	pDevice->m_pDevice->Unacquire();

	// Reset the TriggerTable
	memset( pDevice->m_TriggerTable, 0, sizeof(TriggerObject*) * MAX_OBJECT_BINDINGS );

	// Setup the object formats.
	memset( objectFormats, 0, sizeof(DIOBJECTDATAFORMAT)*MAX_OBJECT_BINDINGS );

	iTrigger = 0;
	curOffset = 0;

	for(pos=pDevice->m_Triggers; pos; )
	{
		pTrigger = pDevice->m_Triggers.GetNext(pos);

		if( iTrigger >= MAX_OBJECT_BINDINGS )
			break;

		// make sure we haven't already created on object data instance for this object
		bDuplicate = FALSE;
		for(pos2=pDevice->m_Triggers; pos2; )
		{
			ptr = pDevice->m_Triggers.GetNext(pos2);

			if( ptr == pTrigger )
				break;

			if(stricmp(ptr->m_TriggerName, pTrigger->m_TriggerName) == 0)
			{
				pTrigger->m_StateIndex = ptr->m_StateIndex;
				bDuplicate = TRUE;
				break;
			}
		}

		if( bDuplicate )
		{
			continue;
		}

		// create the object data instance
		objectFormats[iTrigger].pguid = &pTrigger->m_diGuid;
		objectFormats[iTrigger].dwOfs = curOffset;
		objectFormats[iTrigger].dwType = pTrigger->m_diType;
		pTrigger->m_StateIndex = iTrigger;

		// put the pointer to this trigger in the TriggerTable
		pDevice->m_TriggerTable[curOffset >> 2] = pTrigger;

		++iTrigger;
		curOffset += 4;
	}


	// Set format.
	memset( &format, 0, sizeof(format) );
	format.dwSize = sizeof(DIDATAFORMAT);
	format.dwObjSize = sizeof(DIOBJECTDATAFORMAT);
	format.rgodf = objectFormats;
	format.dwNumObjs = iTrigger;
	format.dwFlags = pDevice->IsJoystick() ? DIDF_ABSAXIS : DIDF_RELAXIS;
	format.dwDataSize = curOffset;

	hResult = pDevice->m_pDevice->SetDataFormat( &format );
	if( hResult != DI_OK )
		return FALSE;

	pDevice->m_StateReadSize = curOffset;

	// Set the device properties
	prop.diph.dwSize = sizeof (DIPROPDWORD);
	prop.diph.dwHeaderSize = sizeof (DIPROPHEADER);
	prop.diph.dwObj = 0;
	prop.diph.dwHow = DIPH_DEVICE;
	prop.dwData = INPUT_BUFFER_SIZE;

	hResult = pDevice->m_pDevice->SetProperty (DIPROP_BUFFERSIZE, &prop.diph);
	hResult = pDevice->m_pDevice->GetProperty (DIPROP_BUFFERSIZE, &prop.diph);
	if( prop.dwData == 0 )
	{
		return FALSE;
	}


	// Init some data in case we can't read the current state.
	for(pos=pDevice->m_Triggers; pos; )
	{
		pTrigger = pDevice->m_Triggers.GetNext(pos);

		if ((pTrigger->m_InputType == Input_RelAxis) || (pTrigger->m_InputType == Input_AbsAxis))
		{
			pTrigger->m_State = ((pTrigger->m_DataMax - pTrigger->m_DataMin) / 2.0f) + pTrigger->m_fRangeScalePreCenterOffset;
			pTrigger->m_PrevState = pTrigger->m_State;
			pTrigger->m_BaseState = pTrigger->m_PrevState;
		}
		else
		{
			pTrigger->m_State = 0.0f;
			pTrigger->m_PrevState = 0.0f;
			pTrigger->m_BaseState = 0.0f;
		}
	}


	// Re-acquire it.
	hResult = pDevice->m_pDevice->Acquire();
	if(hResult != DI_OK)
	{
		if(hResult == DIERR_OTHERAPPHASPRIO)
		{
			// Not a big deal, we just don't have focus.
			return TRUE;
		}
		else
		{
			// Some bad error..
			return FALSE;
		}
	}

	// Init the states of all the triggers for this device
	hResult = pDevice->m_pDevice->GetDeviceState( pDevice->m_StateReadSize, dwStates );
	if( hResult == DI_OK )
	{
		for(pos=pDevice->m_Triggers; pos; )
		{
			pTrigger = pDevice->m_Triggers.GetNext(pos);
			if ((pTrigger->m_InputType == Input_RelAxis) || (pTrigger->m_InputType == Input_AbsAxis))
			{
				pTrigger->m_State = (float)dwStates[pTrigger->m_StateIndex];
				pTrigger->m_PrevState = pTrigger->m_State;
				pTrigger->m_BaseState = pTrigger->m_State;
			}
			else
			{
				pTrigger->m_State = (float)dwStates[pTrigger->m_StateIndex];
				pTrigger->m_PrevState = pTrigger->m_State;
				pTrigger->m_BaseState = pTrigger->m_State;
			}
		}
	}

	return TRUE;
}


// FUNCTION: LITHTECH 0x0043eee0
static BOOL CALLBACK DeviceEnumCallback( LPCDIDEVICEINSTANCE pDevice, LPVOID pvRef )
{
	DeviceDef *pDef;

	pDef = new DeviceDef;
	memset( pDef, 0, sizeof(DeviceDef) );
	strncpy( pDef->m_InstanceName, pDevice->tszInstanceName, INPUTNAME_LEN );
	memcpy( &pDef->m_InstanceGuid, &pDevice->guidInstance, sizeof(GUID) );
	pDef->m_DeviceType = pDevice->dwDevType;

	pDef->m_pNext = g_pDeviceHead;
	g_pDeviceHead = pDef;

	return DIENUM_CONTINUE;
}


// FUNCTION: LITHTECH 0x0043f090
static char* GetDeviceObjectTypeString(LPCDIDEVICEOBJECTINSTANCE pObj)
{
	if(pObj->guidType == GUID_XAxis)
		return "X axis";

	else if(pObj->guidType == GUID_YAxis)
		return "Y axis";

	else if(pObj->guidType == GUID_ZAxis)
		return "Z axis";

	else if(pObj->guidType == GUID_RxAxis)
		return "X axis rotation";

	else if(pObj->guidType == GUID_RyAxis)
		return "Y axis rotation";

	else if(pObj->guidType == GUID_RzAxis)
		return "Z axis rotation";

	else if(pObj->guidType == GUID_Slider)
		return "slider";

	else if(pObj->guidType == GUID_Button)
		return "button";

	else if(pObj->guidType == GUID_Key)
		return "key";

	else if(pObj->guidType == GUID_POV)
		return "POV hat";

	else
		return "unknown";
}


// FUNCTION: LITHTECH 0x0043f060
static BOOL CALLBACK ListDeviceObjectsEnumCallback(LPCDIDEVICEOBJECTINSTANCE pObj, LPVOID pvRef)
{
	dsi_ConsolePrint("-    Object %s (type: %s)", pObj->tszName, GetDeviceObjectTypeString(pObj));
	return DIENUM_CONTINUE;
}


// FUNCTION: LITHTECH 0x0043eff0
static BOOL CALLBACK ListDevicesEnumCallback(LPCDIDEVICEINSTANCE pDevice, LPVOID pvRef)
{
	HRESULT hResult;
	LPDIRECTINPUTDEVICE pDIDevice;

	// Try to create it.
	hResult = g_pDirectInput->CreateDevice(pDevice->guidInstance, &pDIDevice, LTNULL);
	if(hResult == DI_OK)
	{
		dsi_ConsolePrint("- Device %s", pDevice->tszInstanceName);

		hResult = pDIDevice->EnumObjects(ListDeviceObjectsEnumCallback, LTNULL, DIDFT_ALL);

		pDIDevice->Release();
	}
	else
	{
		dsi_ConsolePrint("- Device %s (unable to create)", pDevice->tszInstanceName);
	}

	return DIENUM_CONTINUE;
}



// --------------------------------------------------------------------- //
// Term..
// --------------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x0043ec80
void input_Term(InputMgr *pMgr)
{
	DeviceDef		*pDef, *pNext;
	ActionDef		*pCurAction, *pNextAction;

	g_pInputConsoleState = LTNULL;

	// Clear the device defs.
	pDef = g_pDeviceHead;
	while( pDef )
	{
		pNext = pDef->m_pNext;

		if( pDef->m_pDevice )
		{
			pDef->m_pDevice->Unacquire();
			pDef->m_pDevice->Release();
		}

		delete pDef;

		pDef = pNext;
	}
	g_pDeviceHead = LTNULL;

	// clear the effects
	CJoystickEffect* pEffect = g_pJoystickEffects;
	CJoystickEffect* pPrev = LTNULL;
	while (pEffect)
	{
		if (pEffect->m_pEffect)
		{
			pEffect->m_pEffect->Unload();
			pEffect->m_pEffect->Release();
		}
		pEffect->m_pEffect = LTNULL;
		pPrev = pEffect;
		pEffect = pEffect->m_pNext;
		delete pPrev;
	}
	g_pJoystickEffects = LTNULL;


	// Free DirectInput stuff.
	if( g_pDirectInput )
	{
		g_pDirectInput->Release();
		g_pDirectInput = LTNULL;
	}


	// Free all action defs.
	pCurAction = g_ActionDefHead.m_pNext;
	while(pCurAction != &g_ActionDefHead)
	{
		pNextAction = pCurAction->m_pNext;
		delete pCurAction;
		pCurAction = pNextAction;
	}

	g_ActionDefHead.m_pPrev = g_ActionDefHead.m_pNext = &g_ActionDefHead;
}


// --------------------------------------------------------------------- //
// Init input.
// --------------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x0043ee70
LTBOOL input_Init(InputMgr *pMgr, ConsoleState *pState)
{
	HRESULT		hResult;

	g_InputHWND = (HWND)dsi_GetMainWindow();
	g_pInputConsoleState = pState;

	// Create the DirectInput device.
	hResult = DirectInputCreate( (HINSTANCE)dsi_GetInstanceHandle(), DIRECTINPUT_VERSION, &g_pDirectInput, LTNULL );
	if( hResult != DI_OK )
	{
		// Try DirectX 3.
		hResult = DirectInputCreate( (HINSTANCE)dsi_GetInstanceHandle(), 0x0300, &g_pDirectInput, LTNULL );
		if( hResult != DI_OK )
			return FALSE;
	}

	// Enumerate devices so we know what's there.
	g_pDirectInput->EnumDevices( 0, DeviceEnumCallback, LTNULL, DIEDFL_ATTACHEDONLY );

	return TRUE;
}


// FUNCTION: LITHTECH 0x0043efa0
LTBOOL input_IsInitted(InputMgr *pMgr)
{
	return g_pDirectInput != 0;
}


// FUNCTION: LITHTECH 0x0043efb0
void input_ListDevices(InputMgr *pMgr)
{
	if(!g_pDirectInput)
	{
		dsi_ConsolePrint("Input not initialized");
	}

	dsi_ConsolePrint("------------ Input devices ------------");
	g_pDirectInput->EnumDevices(0, ListDevicesEnumCallback, LTNULL, 0);
}


// --------------------------------------------------------------------- //
// Plays a force feedback effect if loaded
// --------------------------------------------------------------------- //

// Folded with other `return 0` functions at 0x0043dac0.
long input_PlayJoystickEffect( InputMgr *pMgr, char* strEffectName, float x, float y )
{
	return DI_OK;
}

// --------------------------------------------------------------------- //
// Enables input from a particular device.
// --------------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x0043f1b0
LTBOOL input_EnableDevice( InputMgr *pMgr, char *pDeviceName )
{
	DeviceDef *pDevice;
	HRESULT	hResult;
	DIDATAFORMAT format;
	GUID theGuid;
	LPDIRECTINPUTDEVICE pDIDevice;


	if(!input_IsInitted(pMgr))
		goto Failed;


	// Find the device.
	pDevice = input_FindDeviceByName(pDeviceName);
	if( !pDevice )
		goto Failed;

	// Create the device if it hasn't been created yet.
	if( !pDevice->m_pDevice )
	{
		// Do special stuff if it's the mouse (fixes the USB mouse lockups).
		theGuid = pDevice->m_InstanceGuid;
		pDevice->m_bSysMouse = FALSE;
		if(stricmp(pDeviceName, "##mouse")==0)
		{
			pDevice->m_bSysMouse = TRUE;
			theGuid = GUID_SysMouse;
		}
		else if(stricmp(pDeviceName, "##keyboard")==0)
		{
			theGuid = GUID_SysKeyboard;
		}

		hResult = g_pDirectInput->CreateDevice( theGuid, &pDIDevice, LTNULL );
		if( hResult != DI_OK )
			goto Failed;

		hResult = pDIDevice->QueryInterface( IID_IDirectInputDevice2, (void**)&pDevice->m_pDevice );
		if( hResult != DI_OK )
		{
			pDIDevice->Release();
			return FALSE;
		}

		// Set cooperative level.
		if(pDevice->IsJoystick())
		{
			hResult = pDevice->m_pDevice->SetCooperativeLevel( g_InputHWND, DISCL_EXCLUSIVE|DISCL_FOREGROUND );
		}
		else
		{
			hResult = pDevice->m_pDevice->SetCooperativeLevel( g_InputHWND, DISCL_NONEXCLUSIVE|DISCL_FOREGROUND );
		}

		if( hResult != DI_OK )
		{
			pDevice->m_pDevice->Release();
			pDevice->m_pDevice = LTNULL;
			return FALSE;
		}

		// Set the data format.  It starts with no device objects used so this part is easy!
		memset( &format, 0, sizeof(format) );
		format.dwSize = sizeof(DIDATAFORMAT);
		format.dwObjSize = sizeof(DIOBJECTDATAFORMAT);
		format.dwFlags = DIDF_RELAXIS;	// We like relative axis.

		if(pDevice->IsJoystick())
		{
			hResult = pDevice->m_pDevice->SetDataFormat( &c_dfDIJoystick );
		}
		else
		{
			hResult = pDevice->m_pDevice->SetDataFormat( &format );
		}

		if( hResult != DI_OK )
		{
			pDevice->m_pDevice->Release();
			pDevice->m_pDevice = LTNULL;
Failed:
			return FALSE;
		}

		// Normally, we would acquire it at this point, but since nothing is bound to
		// anything on this device, we'll wait to acquire it.
		hResult = pDevice->m_pDevice->Acquire();
	}

	return TRUE;
}


// --------------------------------------------------------------------- //
// This is the main routine that reads input from all devices and sets
// any actions that are on.
// --------------------------------------------------------------------- //
// FUNCTION: LITHTECH 0x0043f490
void input_ReadInput( InputMgr *pMgr, uint8 *pActionsOn, float axisOffsets[3] )
{
	DeviceDef *pDevice;
	TriggerObject *pTrigger;
	TriggerAction *pAction;
	DIDEVICEOBJECTDATA data[INPUT_BUFFER_SIZE];
	HRESULT hResult;
	uint32 i;
	unsigned long nEvents;
	int actionCode;
	float addAmt;
	uint32 dwTickCount;
	GPOS pos;
	CTriggerActionConsoleVar* pTrigConVar;

	// clear the axis array
	axisOffsets[0] = axisOffsets[1] = axisOffsets[2] = 0.0f;

	// clear all of the axis console variables
	pTrigConVar = g_lstTriggerActionConsoleVariables;
	while(pTrigConVar != LTNULL)
	{
		pTrigConVar->SetFloat(0.0);
		pTrigConVar->InitCommandVar();
		pTrigConVar = pTrigConVar->m_pNext;
	}

	// Get the time stamp for this frame...
	dwTickCount = GetTickCount( );

	// Query each device.
	pDevice = g_pDeviceHead;
	while( pDevice )
	{
		while( pDevice->IsEnabled() )
		{
			// Check for JoystickDisable..
			if(pDevice->IsJoystick())
			{
				if(g_CV_JoystickDisable)
					break;

				// Poll the device if necessary
				hResult = pDevice->m_pDevice->Poll();
			}

			memset (data, 0, sizeof(data));

			if(pDevice->m_bSysMouse)
			{
				// This fixes the USB mouse lockups.
				nEvents = 1;
			}
			else
			{
				nEvents = INPUT_BUFFER_SIZE;
			}

			hResult = pDevice->m_pDevice->GetDeviceData( sizeof(DIDEVICEOBJECTDATA), data, &nEvents, 0 );

			if( hResult != DI_OK )
			{
				if( hResult == DI_BUFFEROVERFLOW )
				{
					dsi_ConsolePrint ("Input buffer overflow on %s", pDevice->m_InstanceName);
				}
				else
				{
					// Attempt to acquire it and retry .. maybe it got lost.
					hResult = pDevice->m_pDevice->Acquire();

					hResult = pDevice->m_pDevice->GetDeviceData( sizeof(DIDEVICEOBJECTDATA), data, &nEvents, 0 );
				}
			}

			if(hResult != DI_OK && hResult != DI_BUFFEROVERFLOW)
				break;

			// Go through each event and update the associated triggers
			// (unless it's a key up event - handle those at the end in
			//  case there was a down-up combination in the same frame)
			for(i = 0; i < nEvents; i++)
			{
				if((data[i].dwOfs>>2) >= pDevice->m_Triggers.GetSize())
					continue;

				// get the trigger associated with this event
				pTrigger = pDevice->m_TriggerTable[data[i].dwOfs >> 2];

				if(g_CV_InputDebug)
				{
					dsi_ConsolePrint("%s (%s) generated %d", pTrigger->m_TriggerName, pTrigger->m_RealName, data[i].dwData);
				}

				// See if the trigger is a button...
				if ( pTrigger->m_InputType == Input_PushButton )
				{
					// If the button was up, then worry about it later, otherwise save the data...
					if( data[i].dwData != 0 )
					{
						// Did it just go down?
						if(!pTrigger->m_State)
						{
							// Maybe trigger a console command..
							pTrigger->m_bJustWentDown = TRUE;
						}

						pTrigger->m_State = (float)data[i].dwData;
					}

					continue;
				}

				// See if the trigger is a hat...
				if ( pTrigger->m_InputType == Input_Pov )
				{
					pTrigger->m_State = (float)data[i].dwData;
					continue;
				}

				// Process all the non-buttons...
				pTrigger->m_dwBaseUpdateTime = pTrigger->m_dwPrevUpdateTime;
				pTrigger->m_dwPrevUpdateTime = pTrigger->m_dwUpdateTime;
				pTrigger->m_dwUpdateTime = dwTickCount;

				// set the trigger's state
				pTrigger->m_BaseState = pTrigger->m_PrevState;
				pTrigger->m_PrevState = pTrigger->m_State;
				pTrigger->m_State = (float)((int32)(data[i].dwData));
			}


			// Go thru each trigger and check the state.
			for(pos=pDevice->m_Triggers; pos; )
			{
				pTrigger = pDevice->m_Triggers.GetNext(pos);

				// Interpolate the non-pushbuttons...
				if(pTrigger->m_InputType == Input_PushButton)
				{
					// Scale the state...
					addAmt = pTrigger->m_State * pTrigger->m_Scale;
				}
				else
				{
					float fCurTriggerState = pTrigger->m_State;

					if( pTrigger->m_InputType != Input_Pov )
					{
						// Summed one term at a time (a single expression reorders the fadds).
						fCurTriggerState = pTrigger->m_PrevState;
						fCurTriggerState += pTrigger->m_BaseState;
						fCurTriggerState += pTrigger->m_State;
						fCurTriggerState /= 3.0f;
						float fInterpolant = (float)g_CV_InputRate / 100.0f;
						fCurTriggerState = LTLERP(pTrigger->m_State, fCurTriggerState, fInterpolant);

						// Zero out relative axes based on the input rate
						if (pTrigger->m_InputType == Input_RelAxis)
						{
							// Simulate a zero sample based on the input rate
							if ((dwTickCount - pTrigger->m_dwUpdateTime) > ((uint32)g_CV_InputRate / 10))
							{
								pTrigger->m_BaseState = pTrigger->m_PrevState;
								pTrigger->m_PrevState = pTrigger->m_State;
								pTrigger->m_State = 0.0f;

								pTrigger->m_dwBaseUpdateTime = pTrigger->m_dwPrevUpdateTime;
								pTrigger->m_dwPrevUpdateTime = pTrigger->m_dwUpdateTime;
								pTrigger->m_dwUpdateTime = dwTickCount;
							}
						}
					}

					// Scale the state...
					addAmt = LTCLAMP(fCurTriggerState, pTrigger->m_DataMin, pTrigger->m_DataMax );

					if (pTrigger->m_fRangeScalePreCenterOffset != 0.0)
					{
						if (addAmt <= pTrigger->m_fRangeScalePreCenter)
						{
							addAmt = pTrigger->m_fRangeScaleOffsetLo + (pTrigger->m_fRangeScaleMultiplierLo * addAmt);
						}
						else
						{
							addAmt = pTrigger->m_fRangeScaleOffsetHi + (pTrigger->m_fRangeScaleMultiplierHi * addAmt);
						}
					}
					else
					{
						addAmt = pTrigger->m_fRangeScaleOffset + (pTrigger->m_fRangeScaleMultiplier * addAmt);
					}

					// NOTE : the RangeScale stuff will not work well if there are mutiple addAmt's
					addAmt = addAmt * pTrigger->m_Scale;
				}


				// Hit all its actions.
				pAction = pTrigger->m_pActionHead;
				while( pAction )
				{
					if(pAction->m_pConsoleString && pTrigger->m_bJustWentDown && g_pInputConsoleState)
					{
						cc_HandleCommand(g_pInputConsoleState, pAction->m_pConsoleString);
					}
					else if(pAction->m_pAction)
					{
						// Map the trigger state to the action!
						actionCode = pAction->m_pAction->m_ActionCode;
						if(pTrigger->m_InputType == Input_RelAxis && actionCode < 0)
						{
							if( actionCode == -1 )
								axisOffsets[0] += addAmt;
							else if( actionCode == -2 )
								axisOffsets[1] += addAmt;
							else if( actionCode == -3 )
								axisOffsets[2] += addAmt;
							else if( pAction->m_pConsoleVar != LTNULL)
							{
								pAction->m_pConsoleVar->SetFloat(pAction->m_pConsoleVar->GetFloat() + addAmt);
								if (g_CV_InputDebug) dsi_ConsolePrint("rel Trigger %s set console var %s to %22.12f", pTrigger->m_TriggerName, pAction->m_pConsoleVar->GetCommandName(), (double)(pAction->m_pConsoleVar->GetFloat()));
							}
						}
						else if(pTrigger->m_InputType == Input_AbsAxis )
						{
							if( actionCode < 0 )
							{
								if( actionCode == -1 )
									axisOffsets[0] += addAmt;
								else if( actionCode == -2 )
									axisOffsets[1] += addAmt;
								else if( actionCode == -3 )
									axisOffsets[2] += addAmt;
								else if( pAction->m_pConsoleVar != LTNULL)
								{
									pAction->m_pConsoleVar->SetFloat(pAction->m_pConsoleVar->GetFloat() + addAmt);
									if (g_CV_InputDebug) dsi_ConsolePrint("abs Trigger %s set console var %s to %22.12f", pTrigger->m_TriggerName, pAction->m_pConsoleVar->GetCommandName(), (double)(pAction->m_pConsoleVar->GetFloat()));
								}
							}
							else if( addAmt >= pAction->m_RangeLow && addAmt <= pAction->m_RangeHigh )
							{
								pActionsOn[actionCode] |= 1;
							}
						}
						else if ((pTrigger->m_InputType == Input_Pov) && (pAction->m_pConsoleVar != LTNULL))
						{
							pAction->m_pConsoleVar->SetFloat(pAction->m_pConsoleVar->GetFloat() + addAmt);
						}
						else if( pTrigger->m_InputType == Input_Pov && actionCode < 0 && addAmt != 65536.0f )
						{
							if( addAmt >= 4500 && addAmt <= 13500 )
							{
								axisOffsets[0] += 30;
							}
							else if( addAmt >= 22500 && addAmt <= 31500 )
							{
								axisOffsets[0] -= 30;
							}

							if( (addAmt >= 31500 && addAmt <= 36000) || (addAmt <= 4500 && addAmt >= 0) )
							{
								axisOffsets[1] -= 30;
							}

							if( addAmt >= 13500 && addAmt <= 22500 )
							{
								axisOffsets[1] += 30;
							}
						}
						else if(pAction->m_RangeLow != 0.0f || pAction->m_RangeHigh != 0.0f)
						{
							if( addAmt >= pAction->m_RangeLow && addAmt <= pAction->m_RangeHigh )
							{
								pActionsOn[actionCode] |= 1;
							}
						}
						else if( actionCode >= 0 )
						{
							pActionsOn[actionCode] |= pTrigger->m_State != 0.0f;
						}
					}

					pAction = pAction->m_pNext;
				}

				// Clear this..
				pTrigger->m_bJustWentDown = FALSE;
			}

			// Go through any key up events and update the associated triggers
			// Also clear any relative axis data...
			for( i = 0; i < nEvents; i++ )
			{
				if((data[i].dwOfs>>2) >= pDevice->m_Triggers.GetSize())
					continue;

				// get the trigger associated with this event
				pTrigger = pDevice->m_TriggerTable[data[i].dwOfs >> 2];

				// see if the trigger is a button and if it's state has changed to 'up'
				if( pTrigger->m_InputType == Input_PushButton && data[i].dwData == 0 )
				{
					pTrigger->m_State = (float)data[i].dwData;
				}
			}

			if(nEvents == 0 || !pDevice->m_bSysMouse)
				break;
		}

		// Update the last time...
		pDevice->m_dwLastTime = dwTickCount;

		pDevice = pDevice->m_pNext;
	}
}


// --------------------------------------------------------------------- //
// Flush the DirectInput buffers
// --------------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x0043fde0
LTBOOL input_FlushInputBuffers(InputMgr *pMgr)
{
	LTBOOL bSuccess = TRUE;

	DeviceDef* pDef = g_pDeviceHead;
	while( pDef )
	{
		if ( pDef->IsEnabled() )
		{
			unsigned long dwItems = INFINITE;
			HRESULT hResult = pDef->m_pDevice->GetDeviceData( sizeof(DIDEVICEOBJECTDATA), LTNULL, &dwItems, 0 );

			if( hResult != DI_OK && hResult != DI_BUFFEROVERFLOW )
			{
				bSuccess = FALSE;
			}
		}

		pDef = pDef->m_pNext;
	}

	return bSuccess;
}

// FUNCTION: LITHTECH 0x0043fe30
static LTRESULT input_ClearInput()
{
	DeviceDef *pDevice;

	pDevice = g_pDeviceHead;
	while( pDevice )
	{
		if( pDevice->IsEnabled() )
		{
			input_SetupDeviceFormats(pDevice);
		}
		pDevice = pDevice->m_pNext;
	}
	return LT_OK;
}


// --------------------------------------------------------------------- //
// You want to add action defs first so there will be something to bind to!
// --------------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x00440170
void input_AddAction(InputMgr *pMgr, char *pActionName, int code)
{
	ActionDef	*pDef;

	if(code >= 256)
		return;

	// If there's already an action with that name, change its code.
	pDef = input_FindAction(pActionName);
	if(pDef)
	{
		pDef->m_ActionCode = code;
		return;
	}

	// Ok, add a new one.
	pDef = new ActionDef;
	pDef->m_pPrev = g_ActionDefHead.m_pPrev;
	pDef->m_pNext = &g_ActionDefHead;
	pDef->m_pPrev->m_pNext = pDef->m_pNext->m_pPrev = pDef;
	strncpy(pDef->m_ActionName, pActionName, MAX_ACTIONNAME_LEN);
	pDef->m_ActionCode = code;
}


// --------------------------------------------------------------------- //
// Clear bindings for an input trigger.
// Note:  It would probably be better to have this routine remove the trigger completely,
//        but it's not too big a deal right now.
// --------------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x00440220
LTBOOL input_ClearBindings( InputMgr *pMgr, char *pDeviceName, char *pTriggerName )
{
	DeviceDef *pDef = input_FindDeviceByName(pDeviceName);
	if(!pDef)
		return FALSE;

	if(!pDef->IsEnabled())
		return FALSE;

	TriggerObject *pTrigger = input_FindTrigger(pDef, pTriggerName);
	if(!pTrigger)
		return FALSE;

	// Delete the trigger and remove it from the list.
	pDef->m_Triggers.RemoveAt(pTrigger);
	delete pTrigger;

	if(g_DebugLevel > 10)
		dsi_ConsolePrint("Cleared bindings for %s (on %s)", pTriggerName, pDeviceName);

	// Re-setup the device data formats / possibly acquire the device.
	return input_SetupDeviceFormats(pDef);
}


// --------------------------------------------------------------------- //
// Add a binding for a device.
// --------------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x004403c0
LTBOOL input_AddBinding(InputMgr *pMgr,
	char *pDeviceName, char *pTriggerName, char *pActionName,
	float rangeLow, float rangeHigh)
{
	TriggerObject				*pTrigger;
	TriggerAction				*pAction;
	CTriggerActionConsoleVar	*pConVar = LTNULL;
	ActionDef					*pActionDef;
	DeviceDef					*pDevice;
	float temp;


	if(rangeLow > rangeHigh)
	{
		temp = rangeLow;
		rangeLow = rangeHigh;
		rangeHigh = temp;
	}

	if (stricmp(pDeviceName, "Joystick 1") == 0)
	{
	}


	pDevice = input_FindDeviceByName(pDeviceName);
	if( !pDevice )
		return FALSE;

	if( !pDevice->IsEnabled() )
		return FALSE;

	pTrigger = input_FindTrigger(pDevice, pTriggerName);
	if(!pTrigger)
	{
		pTrigger = input_AddTrigger(pDevice, pTriggerName);
		if(!pTrigger)
			return FALSE;
	}

	if(strlen(pActionName) > 1 && pActionName[0] == '*')
	{
		pActionDef = LTNULL;
	}
	else
	{
		// find the action
		pActionDef = input_FindAction(pActionName);
		if( !pActionDef )
			return FALSE;

	    // If we are using a console variable to send input data to the game set it up
		if (pActionDef->m_ActionCode <= -10000)
		{
			// make a new console var
			pConVar = new CTriggerActionConsoleVar;

			// add to list of console vars for trigger actions
			pConVar->m_pNext = g_lstTriggerActionConsoleVariables;
			g_lstTriggerActionConsoleVariables = pConVar;
		}
	}

	// Add the action.
	pAction = new TriggerAction;
	pAction->m_pNext = pTrigger->m_pActionHead;
	pAction->m_RangeLow = rangeLow;
	pAction->m_RangeHigh = rangeHigh;
	pAction->m_pConsoleVar = pConVar;

    if (pConVar != LTNULL) pConVar->m_pTriggerAction = pAction;

	// If pActionDef is LTNULL then it's a console string.
	pAction->m_pAction = pActionDef;
	if(!pActionDef)
	{
		pAction->m_pConsoleString = new char[strlen(pActionName)];
		if(!pAction->m_pConsoleString)
		{
			delete pAction;
			return FALSE;
		}
		strcpy(pAction->m_pConsoleString, &pActionName[1]);
	}

	pTrigger->m_pActionHead = pAction;

	// Re-setup the device data formats / possibly acquire the device.
	if(!input_SetupDeviceFormats(pDevice))
	{
		// Get rid of the trigger we just tried to add.
		if(pTrigger->m_pActionHead == pAction)
		{
			pTrigger->m_pActionHead = pAction->m_pNext;
			delete pAction;
		}

		input_SetupDeviceFormats(pDevice);
		return FALSE;
	}

	if(g_DebugLevel > 10)
		dsi_ConsolePrint("Bound %s (on %s) to action %s", pTriggerName, pDeviceName, pActionName);

	return TRUE;
}

// --------------------------------------------------------------------- //
// Sets the trigger's scale.
// --------------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x00440d90
LTBOOL input_ScaleTrigger( InputMgr *pMgr, char *pDeviceName, char *pTriggerName, float scale, float fRangeScaleMin, float fRangeScaleMax, float fRangeScalePreCenterOffset )
{
	DeviceDef		*pDevice;
	TriggerObject	*pTrigger;

	scale = LTCLAMP(scale, -500.0f, 500.0f);

	pDevice = input_FindDeviceByName(pDeviceName);
	if( !pDevice )
		return FALSE;

	pTrigger = input_FindTrigger(pDevice, pTriggerName);
	if( !pTrigger )
		return FALSE;

	pTrigger->m_Scale = scale;

	if ((fRangeScaleMin != 0.0f) || (fRangeScaleMax != 0.0f))
	{
		pTrigger->m_fRangeScaleMin = fRangeScaleMin;
		pTrigger->m_fRangeScaleMax = fRangeScaleMax;

		float fDataMinMinusMax = pTrigger->m_DataMin - pTrigger->m_DataMax;
		if (fDataMinMinusMax != 0.0f) pTrigger->m_fRangeScaleMultiplier = (fRangeScaleMin - fRangeScaleMax) / fDataMinMinusMax;
		else pTrigger->m_fRangeScaleMultiplier = 1.0f;
		pTrigger->m_fRangeScaleOffset = fRangeScaleMin - (pTrigger->m_fRangeScaleMultiplier * pTrigger->m_DataMin);

		pTrigger->m_fRangeScalePreCenterOffset = fRangeScalePreCenterOffset;
		if (fRangeScalePreCenterOffset != 0.0f)
		{
			float fDataCenter = ((pTrigger->m_DataMax - pTrigger->m_DataMin) / 2.0f) + fRangeScalePreCenterOffset + pTrigger->m_DataMin;
			float fRangeScaleCenter = ((fRangeScaleMax - fRangeScaleMin) / 2.0f) + fRangeScaleMin;

			float fDataMinMinusMaxHi = (fDataCenter - pTrigger->m_DataMax);
			if (fDataMinMinusMaxHi != 0.0f) pTrigger->m_fRangeScaleMultiplierHi = (fRangeScaleCenter - fRangeScaleMax) / fDataMinMinusMaxHi;
			else pTrigger->m_fRangeScaleMultiplierHi = 1.0f;
			pTrigger->m_fRangeScaleOffsetHi = fRangeScaleCenter - (pTrigger->m_fRangeScaleMultiplierHi * fDataCenter);

			float fDataMinMinusMaxLo = (pTrigger->m_DataMin - fDataCenter);
			if (fDataMinMinusMaxLo != 0.0f) pTrigger->m_fRangeScaleMultiplierLo = (fRangeScaleMin - fRangeScaleCenter) / fDataMinMinusMaxLo;
			else pTrigger->m_fRangeScaleMultiplierLo = 1.0f;
			pTrigger->m_fRangeScaleOffsetLo = fRangeScaleMin - (pTrigger->m_fRangeScaleMultiplierLo * pTrigger->m_DataMin);

			pTrigger->m_fRangeScalePreCenter = fDataCenter;
		}
	}

	return TRUE;
}


// --------------------------------------------------------------------- //
// Saves the state of all the input bindings.
// --------------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x00440f60
void input_SaveBindings( FILE *fp )
{
	DeviceDef *pDevice;
	TriggerObject *pTrigger;
	TriggerAction *pAction;
	ActionDef *pCurActionDef;
	char str[1024], str2[1024];
	char *pDeviceName;
	GPOS triggerPos;


	// Save all action defs.
	pCurActionDef = g_ActionDefHead.m_pNext;
	while(pCurActionDef != &g_ActionDefHead)
	{
		fprintf(fp, "AddAction %s %d\n", pCurActionDef->m_ActionName, pCurActionDef->m_ActionCode);
		pCurActionDef = pCurActionDef->m_pNext;
	}


	fprintf(fp, "\n");


	// Save all the active devices.
	pDevice = g_pDeviceHead;
	while( pDevice )
	{
		if(pDevice->IsEnabled() && pDevice->m_Triggers.GetSize() > 0)
		{
			pDeviceName = pDevice->m_pSpecialName ? pDevice->m_pSpecialName : pDevice->m_InstanceName;
			fprintf( fp, "enabledevice \"%s\"\n", pDeviceName );

			// Save all the triggers.
			for(triggerPos=pDevice->m_Triggers; triggerPos; )
			{
				pTrigger = pDevice->m_Triggers.GetNext(triggerPos);

				sprintf(str, "rangebind \"%s\" \"%s\" ", pDeviceName, pTrigger->m_RealName);

				pAction = pTrigger->m_pActionHead;
				while( pAction )
				{
					if(pAction->m_pAction)
						sprintf( str2, "%f %f \"%s\" ", pAction->m_RangeLow, pAction->m_RangeHigh, pAction->m_pAction->m_ActionName );
					else if(pAction->m_pConsoleString)
						sprintf( str2, "%f %f \"*%s\" ", pAction->m_RangeLow, pAction->m_RangeHigh, pAction->m_pConsoleString );
					else
						str2[0] = 0;

					strcat( str, str2 );
					pAction = pAction->m_pNext;
				}

				strcat( str, "\n" );
				fprintf( fp, str );

				if( (pTrigger->m_fRangeScaleMin != 0.0f) || (pTrigger->m_fRangeScaleMax != 0.0f)  || (pTrigger->m_fRangeScalePreCenterOffset != 0.0f))
					fprintf( fp, "rangescale \"%s\" \"%s\" %f %f %f %f\n", pDeviceName, pTrigger->m_RealName, pTrigger->m_Scale, pTrigger->m_fRangeScaleMin, pTrigger->m_fRangeScaleMax, pTrigger->m_fRangeScalePreCenterOffset );
				else if( pTrigger->m_Scale != 1.0f )
					fprintf( fp, "scale \"%s\" \"%s\" %f\n", pDeviceName, pTrigger->m_RealName, pTrigger->m_Scale );

			}
		}

		pDevice = pDevice->m_pNext;
	}
}

// --------------------------------------------------------------------- //
// Device Binding Retrieval.
// --------------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x00441210
static void input_FreeDeviceBindings ( DeviceBinding* pBindings )
{
	DeviceBinding* pBinding = pBindings;
	while( pBinding )
	{
		GameAction* pAction = pBinding->pActionHead;
		while( pAction )
		{
			GameAction* pNext = pAction->pNext;
			delete pAction;
			pAction = pNext;
		}

		DeviceBinding* pNext = pBinding->pNext;
		delete pBinding;
		pBinding = pNext;
	}
}

// FUNCTION: LITHTECH 0x00441250
static DeviceBinding* input_GetDeviceBindings ( uint32 nDevice )
{
	DeviceDef* pDevice;
	DeviceBinding* pBindingsHead = LTNULL;
	TriggerObject* pTrigger;
	GPOS triggerPos;


	pDevice = LTNULL;
	pTrigger = LTNULL;

	// get the device they are looking for
	if( nDevice == DEVICETYPE_KEYBOARD )
	{
		pDevice = input_FindDeviceByType( DIDEVTYPE_KEYBOARD );
	}
	else if( nDevice & DEVICETYPE_MOUSE )
	{
		pDevice = input_FindDeviceByType( DIDEVTYPE_MOUSE );
	}
	else if( nDevice & DEVICETYPE_JOYSTICK )
	{
		pDevice = input_FindDeviceByType( DIDEVTYPE_JOYSTICK );
	}
	else if( nDevice & DEVICETYPE_UNKNOWN )
	{
		pDevice = input_FindDeviceByType( DIDEVTYPE_DEVICE );
	}

	if( !pDevice )
		return LTNULL;

	// go through each trigger, building a list of DeviceBindings to return
	for(triggerPos=pDevice->m_Triggers; triggerPos; )
	{
		pTrigger = pDevice->m_Triggers.GetNext(triggerPos);

		DeviceBinding* pBinding;
		pBinding = new DeviceBinding;
		if( !pBinding )
		{
			input_FreeDeviceBindings(pBindingsHead);
			return LTNULL;
		}

		memset( pBinding, 0, sizeof(DeviceBinding) );

		SAFE_STRCPY( pBinding->strDeviceName, pDevice->m_InstanceName );
		SAFE_STRCPY( pBinding->strTriggerName, pTrigger->m_TriggerName );
		SAFE_STRCPY( pBinding->strRealName, pTrigger->m_RealName );

		// store all the scale information for the trigger
		pBinding->nScale = pTrigger->m_Scale;
		pBinding->nRangeScaleMin = pTrigger->m_fRangeScaleMin;
		pBinding->nRangeScaleMax = pTrigger->m_fRangeScaleMax;
		pBinding->nRangeScalePreCenterOffset = pTrigger->m_fRangeScalePreCenterOffset;

		// go through the actions, adding them to the trigger
		GameAction* pActionHead = LTNULL;
		TriggerAction* pTriggerAction = pTrigger->m_pActionHead;
		while( pTriggerAction )
		{
			if(pTriggerAction->m_pAction)
			{
				GameAction* pNewAction;
				pNewAction = new GameAction;
				if( !pNewAction )
				{
					input_FreeDeviceBindings(pBindingsHead);
					return LTNULL;
				}

				memset( pNewAction, 0, sizeof(GameAction) );

				pNewAction->nActionCode = pTriggerAction->m_pAction->m_ActionCode;
				SAFE_STRCPY(pNewAction->strActionName, pTriggerAction->m_pAction->m_ActionName);
				pNewAction->nRangeLow = pTriggerAction->m_RangeLow;
				pNewAction->nRangeHigh = pTriggerAction->m_RangeHigh;
				pNewAction->pNext = pActionHead;
				pActionHead = pNewAction;
			}

			pTriggerAction = pTriggerAction->m_pNext;
		}

		if( pTriggerAction )
		{
			input_FreeDeviceBindings(pBindingsHead);
			return LTNULL;
		}

		pBinding->pActionHead = pActionHead;

		pBinding->pNext = pBindingsHead;
		pBindingsHead = pBinding;
	}

	return pBindingsHead;
}


// --------------------------------------------------------------------- //
// Device Tracking.
// --------------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x004416d0
static BOOL CALLBACK DeviceObjectCountProc ( LPCDIDEVICEOBJECTINSTANCE pObj, LPVOID pvRef )
{
	uint32* pCount = (uint32*) pvRef;
	(*pCount)++;

	return DIENUM_CONTINUE;
}

// FUNCTION: LITHTECH 0x004416f0
static BOOL CALLBACK EnumTrackObjectProc ( LPCDIDEVICEOBJECTINSTANCE pObj, LPVOID pvRef )
{
	TrackObjectInfo** ppInfo = (TrackObjectInfo**)pvRef;

	memcpy( &(*ppInfo)->guidType, &pObj->guidType, sizeof(GUID) );
	(*ppInfo)->dwType = pObj->dwType;
	SAFE_STRCPY( (*ppInfo)->tszName, pObj->tszName );

	(*ppInfo)++;

	return DIENUM_CONTINUE;
}

// FUNCTION: LITHTECH 0x00441440
static LTBOOL input_StartDeviceTrack( InputMgr *pMgr, uint32 nDevices, uint32 nBufferSize )
{
	if( nBufferSize > MAX_INPUT_BUFFER_SIZE ) return FALSE;

	// this will track the first device found of the requested type

	int i;
	for( i = 0; i < 4; i++ )
	{
		DeviceDef*			pDevice = LTNULL;
		HRESULT				hResult = DI_OK;
		uint32				nObjects = 0;
		TrackObjectInfo*	pInfo = LTNULL;
		DIOBJECTDATAFORMAT* pObjectDataFormats = LTNULL;
		DIDATAFORMAT		deviceDataFormat;
		DIPROPDWORD			prop;

		// check for each possible type of device
		if( i == 0 )
		{
			if( nDevices & DEVICETYPE_KEYBOARD )
			{
				pDevice = input_FindDeviceByType( DIDEVTYPE_KEYBOARD );
			}
			else
			{
				continue;
			}
		}
		else if( i == 1 )
		{
			if( nDevices & DEVICETYPE_MOUSE )
			{
				pDevice = input_FindDeviceByType( DIDEVTYPE_MOUSE );
			}
			else
			{
				continue;
			}
		}
		else if( i == 2 )
		{
			if( nDevices & DEVICETYPE_JOYSTICK )
			{
				pDevice = input_FindDeviceByType( DIDEVTYPE_JOYSTICK );
			}
			else
			{
				continue;
			}
		}
		else if( i == 3 )
		{
			if( nDevices & DEVICETYPE_UNKNOWN )
			{
				pDevice = input_FindDeviceByType( DIDEVTYPE_DEVICE );
			}
			else
			{
				continue;
			}
		}

		if( !pDevice ) continue;

		// we now have a device - make sure it's been created
		if( !pDevice->IsEnabled() )
		{
			input_EnableDevice( pMgr, pDevice->m_InstanceName );
			if( !pDevice->IsEnabled() ) break;
			pDevice->m_bTrackingOnly = TRUE;
		}

		// unacquire the device
		pDevice->m_pDevice->Unacquire();

		// count how many objects are on this device
		hResult = pDevice->m_pDevice->EnumObjects( DeviceObjectCountProc, &nObjects, DIDFT_AXIS|DIDFT_BUTTON|DIDFT_POV );
		if( hResult != DI_OK || nObjects == 0 ) break;

		// now enumerate through all objects on the device
		pDevice->m_pTrackObjects = new TrackObjectInfo [nObjects];
		if( !pDevice->m_pTrackObjects ) break;

		pInfo = pDevice->m_pTrackObjects;
		hResult = pDevice->m_pDevice->EnumObjects( EnumTrackObjectProc, &pInfo, DIDFT_AXIS|DIDFT_BUTTON|DIDFT_POV );
		if( hResult != DI_OK )
		{
			delete [] pDevice->m_pTrackObjects;
			pDevice->m_pTrackObjects = LTNULL;
			break;
		}

		// set up the object formats for each object
		pObjectDataFormats = new DIOBJECTDATAFORMAT [nObjects];
		if( !pObjectDataFormats )
		{
			delete [] pDevice->m_pTrackObjects;
			pDevice->m_pTrackObjects = LTNULL;
			break;
		}
		for( uint32 i = 0; i < nObjects; i++ )
		{
			pObjectDataFormats[i].pguid = &pDevice->m_pTrackObjects[i].guidType;
			pObjectDataFormats[i].dwOfs = i << 2;
			pObjectDataFormats[i].dwType = pDevice->m_pTrackObjects[i].dwType;
			pObjectDataFormats[i].dwFlags = 0;
		}

		// set the format for the device
		deviceDataFormat.dwSize = sizeof(DIDATAFORMAT);
		deviceDataFormat.dwObjSize = sizeof(DIOBJECTDATAFORMAT);
		deviceDataFormat.dwFlags = pDevice->IsJoystick() ? DIDF_ABSAXIS : DIDF_RELAXIS;
		deviceDataFormat.dwDataSize = nObjects << 2;
		deviceDataFormat.dwNumObjs = nObjects;
		deviceDataFormat.rgodf = pObjectDataFormats;
		hResult = pDevice->m_pDevice->SetDataFormat (&deviceDataFormat);

		delete [] pObjectDataFormats;
		if( hResult != DI_OK )
		{
			delete [] pDevice->m_pTrackObjects;
			pDevice->m_pTrackObjects = LTNULL;
			break;
		}

		// set the buffer size
		prop.diph.dwSize = sizeof (DIPROPDWORD);
		prop.diph.dwHeaderSize = sizeof (DIPROPHEADER);
		prop.diph.dwObj = 0;
		prop.diph.dwHow = DIPH_DEVICE;
		prop.dwData = 8; // We don't need a buffer very big.

		hResult = pDevice->m_pDevice->SetProperty (DIPROP_BUFFERSIZE, &prop.diph);

		// set the tracking flag
		pDevice->m_bTracking = TRUE;
		pDevice->m_nTrackObjects = nObjects;

		// acquire the device again
		hResult = pDevice->m_pDevice->Acquire();
	}

	if( i < 4 ) return FALSE;

	return TRUE;
}

// FUNCTION: LITHTECH 0x00441750
static LTBOOL input_TrackDevice( DeviceInput *pInputArray, uint32 *pnInOut )
{
	HRESULT				hResult;
	DIDEVICEOBJECTDATA	data[MAX_INPUT_BUFFER_SIZE];
	unsigned long				nEvents;
	uint32				nArraySize;

	nArraySize = *pnInOut;
	*pnInOut = 0;

	DeviceDef* pDevice = g_pDeviceHead;
	while( pDevice )
	{
		if( pDevice->m_bTracking && pDevice->m_pTrackObjects )
		{
			// Poll the device if necessary
			if(pDevice->IsJoystick())
			{
				hResult = pDevice->m_pDevice->Poll();
			}

			memset (data, 0, sizeof(DIDEVICEOBJECTDATA) * MAX_INPUT_BUFFER_SIZE );
			nEvents = MAX_INPUT_BUFFER_SIZE;
			hResult = pDevice->m_pDevice->GetDeviceData( sizeof(DIDEVICEOBJECTDATA), data, &nEvents, 0 );
			if( hResult != DI_OK )
			{
				if( hResult == DI_BUFFEROVERFLOW )
				{
					dsi_ConsolePrint ("Input buffer overflow on %s", pDevice->m_InstanceName);
				}
				else
				{
					// Attempt to acquire it and retry .. maybe it got lost.
					hResult = pDevice->m_pDevice->Acquire();
					hResult = pDevice->m_pDevice->GetDeviceData( sizeof(DIDEVICEOBJECTDATA), data, &nEvents, 0 );
				}
			}

			if( hResult == DI_OK || hResult == DI_BUFFEROVERFLOW )
			{
				// go through events and add them to input array
				for( uint32 i = 0; i < nEvents; i++ )
				{
					if( *pnInOut == nArraySize ) return TRUE;

					switch( GET_DIDEVICE_TYPE( pDevice->m_DeviceType ) )
					{
						case DIDEVTYPE_KEYBOARD:	pInputArray[*pnInOut].m_DeviceType = DEVICETYPE_KEYBOARD; break;
						case DIDEVTYPE_MOUSE:		pInputArray[*pnInOut].m_DeviceType = DEVICETYPE_MOUSE; break;
						case DIDEVTYPE_JOYSTICK:	pInputArray[*pnInOut].m_DeviceType = DEVICETYPE_JOYSTICK; break;
						case DIDEVTYPE_DEVICE:		pInputArray[*pnInOut].m_DeviceType = DEVICETYPE_UNKNOWN; break;
					}
					strncpy( pInputArray[*pnInOut].m_DeviceName, pDevice->m_InstanceName, sizeof(pInputArray[*pnInOut].m_DeviceName) - 1 );

					uint32 nOffset = data[i].dwOfs >> 2;
					if(nOffset < pDevice->m_nTrackObjects)
					{
						if(	pDevice->m_pTrackObjects[ nOffset ].guidType == GUID_XAxis )			pInputArray[*pnInOut].m_ControlType = CONTROLTYPE_XAXIS;
						else if( pDevice->m_pTrackObjects[ nOffset ].guidType == GUID_YAxis )		pInputArray[*pnInOut].m_ControlType = CONTROLTYPE_YAXIS;
						else if( pDevice->m_pTrackObjects[ nOffset ].guidType == GUID_ZAxis )		pInputArray[*pnInOut].m_ControlType = CONTROLTYPE_ZAXIS;
						else if( pDevice->m_pTrackObjects[ nOffset ].guidType == GUID_RxAxis )	pInputArray[*pnInOut].m_ControlType = CONTROLTYPE_RXAXIS;
						else if( pDevice->m_pTrackObjects[ nOffset ].guidType == GUID_RyAxis )	pInputArray[*pnInOut].m_ControlType = CONTROLTYPE_RYAXIS;
						else if( pDevice->m_pTrackObjects[ nOffset ].guidType == GUID_RzAxis )	pInputArray[*pnInOut].m_ControlType = CONTROLTYPE_RZAXIS;
						else if( pDevice->m_pTrackObjects[ nOffset ].guidType == GUID_Slider )	pInputArray[*pnInOut].m_ControlType = CONTROLTYPE_SLIDER;
						else if( pDevice->m_pTrackObjects[ nOffset ].guidType == GUID_Button )	pInputArray[*pnInOut].m_ControlType = CONTROLTYPE_BUTTON;
						else if( pDevice->m_pTrackObjects[ nOffset ].guidType == GUID_Key )		pInputArray[*pnInOut].m_ControlType = CONTROLTYPE_KEY;
						else if( pDevice->m_pTrackObjects[ nOffset ].guidType == GUID_POV )		pInputArray[*pnInOut].m_ControlType = CONTROLTYPE_POV;
						else if( pDevice->m_pTrackObjects[ nOffset ].guidType == GUID_Unknown )	pInputArray[*pnInOut].m_ControlType = CONTROLTYPE_UNKNOWN;

						strncpy( pInputArray[*pnInOut].m_ControlName, pDevice->m_pTrackObjects[ nOffset ].tszName, sizeof(pInputArray[*pnInOut].m_ControlName) - 2 );

						uint16 objInstance = (DIDFT_GETINSTANCE(pDevice->m_pTrackObjects[ nOffset ].dwType));
						pInputArray[*pnInOut].m_ControlCode = objInstance;
						pInputArray[*pnInOut].m_InputValue = data[i].dwData;

						(*pnInOut)++;
					}
				}
			}
		}

		pDevice = pDevice->m_pNext;
	}

	return TRUE;
}

// FUNCTION: LITHTECH 0x00441bc0
static LTBOOL input_EndDeviceTrack()
{
	DeviceDef* pDef = g_pDeviceHead;
	while( pDef )
	{
		if( pDef->IsEnabled() )
		{
			pDef->m_bTracking = FALSE;

			// disable any device enabled specifically for device tracking
			if( pDef->m_bTrackingOnly )
			{
				if( pDef->m_pDevice )
				{
					pDef->m_pDevice->Unacquire();
					pDef->m_pDevice->Release();
					pDef->m_pDevice = LTNULL;
				}

				// reset the tracking only flag
				pDef->m_bTrackingOnly = FALSE;

				// remove any tracking object structures
				if( pDef->m_pTrackObjects )
				{
					delete [] pDef->m_pTrackObjects;
					pDef->m_pTrackObjects = LTNULL;
				}
			}
			else
			{
				// re-setup the device
				input_SetupDeviceFormats( pDef );
			}
		}

		pDef = pDef->m_pNext;
	}

	return TRUE;
}


// --------------------------------------------------------------------- //
// Device Object List Retrieval.
// --------------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x00441d60
static BOOL CALLBACK DeviceObjectListProc ( LPCDIDEVICEOBJECTINSTANCE pObj, LPVOID pvRef )
{
	HRESULT hResult;
	DIPROPRANGE dipr;

	DeviceObject*** pppList = (DeviceObject***) pvRef;

	**pppList = new DeviceObject;
	memset( **pppList, 0, sizeof(DeviceObject) );

	if( pObj->guidType == GUID_XAxis )			(**pppList)->m_ObjectType = CONTROLTYPE_XAXIS;
	else if( pObj->guidType == GUID_YAxis )		(**pppList)->m_ObjectType = CONTROLTYPE_YAXIS;
	else if( pObj->guidType == GUID_ZAxis )		(**pppList)->m_ObjectType = CONTROLTYPE_ZAXIS;
	else if( pObj->guidType == GUID_RxAxis )	(**pppList)->m_ObjectType = CONTROLTYPE_RXAXIS;
	else if( pObj->guidType == GUID_RyAxis )	(**pppList)->m_ObjectType = CONTROLTYPE_RYAXIS;
	else if( pObj->guidType == GUID_RzAxis )	(**pppList)->m_ObjectType = CONTROLTYPE_RZAXIS;
	else if( pObj->guidType == GUID_Slider )	(**pppList)->m_ObjectType = CONTROLTYPE_SLIDER;
	else if( pObj->guidType == GUID_Button )	(**pppList)->m_ObjectType = CONTROLTYPE_BUTTON;
	else if( pObj->guidType == GUID_Key )		(**pppList)->m_ObjectType = CONTROLTYPE_KEY;
	else if( pObj->guidType == GUID_POV )		(**pppList)->m_ObjectType = CONTROLTYPE_POV;
	else										(**pppList)->m_ObjectType = CONTROLTYPE_UNKNOWN;

	strncpy( (**pppList)->m_ObjectName, pObj->tszName, sizeof((**pppList)->m_ObjectName) - 1 );

	if( g_pCurrentEnumDevice )
	{
		dipr.diph.dwSize = sizeof( DIPROPRANGE );
		dipr.diph.dwHeaderSize = sizeof( DIPROPHEADER );
		dipr.diph.dwObj = pObj->dwType;
		dipr.diph.dwHow = DIPH_BYID;
		hResult = g_pCurrentEnumDevice->m_pDevice->GetProperty( DIPROP_RANGE, &dipr.diph );
		if( hResult == DI_OK )
		{
			(**pppList)->m_RangeLow = (float) dipr.lMin;
			(**pppList)->m_RangeHigh = (float) dipr.lMax;
		}
	}

	*pppList = &((**pppList)->m_pNext);

	return DIENUM_CONTINUE;
}

// FUNCTION: LITHTECH 0x00441c30
static DeviceObject* input_GetDeviceObjects (uint32 nDeviceFlags)
{
	HRESULT			hResult = DI_OK;
	uint32			nCurrentDeviceType = 0;
	DeviceDef*		pDevice = LTNULL;
	DeviceObject*	pList = LTNULL;
	DeviceObject*	pListPtr = LTNULL;
	int				nJoysticksFound = 0;


	// keep looping until no more devices were found (pDevice will be LTNULL)
	for(;;)
	{
		pDevice = LTNULL;

		// get the device they are looking for
		if( nDeviceFlags & DEVICETYPE_KEYBOARD )
		{
			nDeviceFlags &= ~DEVICETYPE_KEYBOARD;
			nCurrentDeviceType = DEVICETYPE_KEYBOARD;
			pDevice = input_FindDeviceByType( DIDEVTYPE_KEYBOARD );
		}
		else if( nDeviceFlags & DEVICETYPE_MOUSE )
		{
			nDeviceFlags &= ~DEVICETYPE_MOUSE;
			nCurrentDeviceType = DEVICETYPE_MOUSE;
			pDevice = input_FindDeviceByType( DIDEVTYPE_MOUSE );
		}
		else if( nDeviceFlags & DEVICETYPE_JOYSTICK )
		{
			nCurrentDeviceType = DEVICETYPE_JOYSTICK;
			pDevice = FindDeviceByTypeWithSkip( DIDEVTYPE_JOYSTICK, nJoysticksFound );
			if (pDevice != LTNULL) nJoysticksFound++;
			else nDeviceFlags &= ~DEVICETYPE_JOYSTICK;
		}
		else if( nDeviceFlags & DEVICETYPE_UNKNOWN )
		{
			nDeviceFlags &= ~DEVICETYPE_UNKNOWN;
			nCurrentDeviceType = DEVICETYPE_UNKNOWN;
			pDevice = input_FindDeviceByType( DIDEVTYPE_DEVICE );
		}

		if( !pDevice ) return pList;

		// enumerate the objects on the device
		if( pDevice->m_pDevice )
		{
			g_pCurrentEnumDevice = pDevice;
			DeviceObject** ppList = &pList;
			hResult = pDevice->m_pDevice->EnumObjects( DeviceObjectListProc, &ppList, DIDFT_ALL );
			g_pCurrentEnumDevice = LTNULL;
		}

		// go through the list and set the device info for each new object we just found
		pListPtr = pList;
		while( pListPtr )
		{
			if( pListPtr->m_DeviceType == 0 )
			{
				pListPtr->m_DeviceType = nCurrentDeviceType;
				strncpy( pListPtr->m_DeviceName, pDevice->m_InstanceName, sizeof(pListPtr->m_DeviceName) - 1 );
			}
			pListPtr = pListPtr->m_pNext;
		}
	}

	return pList;
}

// FUNCTION: LITHTECH 0x00441f90
static void input_FreeDeviceObjects (DeviceObject* pObjectList)
{
	DeviceObject* pObject = pObjectList;
	while( pObject )
	{
		DeviceObject* pNext = pObject->m_pNext;
		delete pObject;
		pObject = pNext;
	}
}



// --------------------------------------------------------------------- //
// Device Helper Functions.
// --------------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x00441fb0
static LTBOOL input_GetDeviceName ( uint32 nDeviceType, char* pStrBuffer, uint32 nBufferSize )
{
	if( !pStrBuffer ) return FALSE;

	DeviceDef* pDevice = LTNULL;

	// get the device they are looking for
	if( nDeviceType == DEVICETYPE_KEYBOARD )
	{
		pDevice = input_FindDeviceByType( DIDEVTYPE_KEYBOARD );
	}
	else if( nDeviceType & DEVICETYPE_MOUSE )
	{
		pDevice = input_FindDeviceByType( DIDEVTYPE_MOUSE );
	}
	else if( nDeviceType & DEVICETYPE_JOYSTICK )
	{
		pDevice = input_FindDeviceByType( DIDEVTYPE_JOYSTICK );
	}
	else if( nDeviceType & DEVICETYPE_UNKNOWN )
	{
		pDevice = input_FindDeviceByType( DIDEVTYPE_DEVICE );
	}

	if( !pDevice ) return FALSE;

	strncpy( pStrBuffer, pDevice->m_InstanceName, nBufferSize - 1 );

	return TRUE;
}

// FUNCTION: LITHTECH 0x00442010
static LTBOOL input_IsDeviceEnabled ( char* pDeviceName )
{
	DeviceDef* pDev = input_FindDeviceByName( pDeviceName );
	if( !pDev ) return FALSE;

	return pDev->IsEnabled();
}

// --------------------------------------------------------------------- //
// Print out the available controls for a device
// --------------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x00442030
static LTBOOL input_ShowDeviceObjects(char* sDeviceName)
{
	HRESULT			hResult = DI_OK;
	DeviceDef*		pDevice = LTNULL;
	DeviceObject*	pList = LTNULL;
	DeviceObject*	pListPtr = LTNULL;

	// make sure a name was passed in
	if ( sDeviceName == LTNULL) return FALSE;

	// get the device with this name
	pDevice = input_FindDeviceByName( sDeviceName );

	// check if our device was found
	if( pDevice == LTNULL ) return FALSE;
	if( pDevice->m_pDevice == LTNULL ) return FALSE;

	// enumerate the objects on the device
	g_pCurrentEnumDevice = pDevice;
	DeviceObject** ppList = &pList;
	hResult = pDevice->m_pDevice->EnumObjects( DeviceObjectListProc, &ppList, DIDFT_ALL );
	if (hResult != DI_OK) return FALSE;
	g_pCurrentEnumDevice = LTNULL;

	// print out device information for each object
	pListPtr = pList;
	while( pListPtr != LTNULL )
	{
		{
			dsi_ConsolePrint("Device = %s  Control = %s Type = %i", pDevice->m_InstanceName, pListPtr->m_ObjectName, (int)pListPtr->m_ObjectType);
		}
		pListPtr = pListPtr->m_pNext;
	}

	// remove all the objects from the list
	DeviceObject* pObject = pList;
	while( pObject != LTNULL )
	{
		DeviceObject* pNext = pObject->m_pNext;
		delete pObject;
		pObject = pNext;
	}

	return TRUE;
}


// --------------------------------------------------------------------- //
// Print out the available input devices to the console
// --------------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x004420f0
static LTBOOL input_ShowInputDevices()
{
	DeviceDef *pDev;

	// Look for it with the normal name.
	pDev = g_pDeviceHead;
	while( pDev )
	{
		dsi_ConsolePrint("Device = %s", pDev->m_InstanceName);

		pDev = pDev->m_pNext;
	}

	return TRUE;
}


// Input managers.
// GLOBAL: LITHTECH 0x004d3830
InputMgr g_MainInputMgr =
{
	input_Init,
	input_Term,
	input_IsInitted,
	input_ListDevices,
	input_PlayJoystickEffect,
	input_ReadInput,
	input_FlushInputBuffers,
	input_ClearInput,
	input_AddAction,
	input_EnableDevice,
	input_ClearBindings,
	input_AddBinding,
	input_ScaleTrigger,
	input_GetDeviceBindings,
	input_FreeDeviceBindings,
	input_StartDeviceTrack,
	input_TrackDevice,
	input_EndDeviceTrack,
	input_GetDeviceObjects,
	input_FreeDeviceObjects,
	input_GetDeviceName,
	input_IsDeviceEnabled,
	input_ShowDeviceObjects,
	input_ShowInputDevices
};

// FUNCTION: LITHTECH 0x00442120
LTRESULT input_GetManager(InputMgr **pMgr)
{
	*pMgr = &g_MainInputMgr;
	return LT_OK;
}
