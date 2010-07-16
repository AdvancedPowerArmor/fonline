#include "StdAfx.h"
#include "Mapper.h"

void _PreRestore()
{
	FOMapper::Self->HexMngr.PreRestore();
}

void _PostRestore()
{
	FOMapper::Self->HexMngr.PostRestore();
	FOMapper::Self->ProcessGameTime();
}

bool FOMapper::SpritesCanDraw=false;
FOMapper* FOMapper::Self=NULL;
FOMapper::FOMapper():Wnd(NULL),DInput(NULL),Keyboard(NULL),Mouse(NULL)
{
	Self=this;
	CmnDiLeft=0;
	CmnDiRight=0;
	CmnDiUp=0;
	CmnDiDown=0;
	CmnDiMleft=0;
	CmnDiMright=0;
	CmnDiMup=0;
	CmnDiMdown=0;
	FPS=0;
	DrawCrExtInfo=0;
	CmnShowPlayerNames=true;
	CmnShowNpcNames=true;
	IsMapperStarted=false;
}

bool FOMapper::Init(HWND wnd)
{
	WriteLog("Mapper initialization...\n");

	STATIC_ASSERT(sizeof(MapObject)-sizeof(MapObject::_RunTime)==MAP_OBJECT_SIZE);
	STATIC_ASSERT(sizeof(ProtoItem)==184);

	Wnd=wnd;
	Keyb::InitKeyb();

	// Options
	OptScrollCheck=false;
	OptScreenClear=true;

	// File manager
	FileManager::SetDataPath(OptFoDataPath.c_str());
	if(!FileManager::LoadDat(OptMasterPath.c_str()))
	{
		MessageBox(Wnd,"MASTER.DAT not found.","Fallout Online Mapper",MB_OK);
		return false;
	}
	if(!FileManager::LoadDat(OptCritterPath.c_str()))
	{
		MessageBox(Wnd,"CRITTER.DAT not found.","Fallout Online Mapper",MB_OK);
		return false;
	}
	if(!FileManager::LoadDat(OptFoPatchPath.c_str()))
	{
		MessageBox(Wnd,"FONLINE.DAT not found.","Fallout Online Mapper",MB_OK);
		return false;
	}

	// Cache
	const char* cache_str=Str::Format("%s%s.cache",FileMngr.GetDataPath(),OptMapperCache.c_str());
	if(!Crypt.SetCacheTable(cache_str))
	{
		WriteLog(__FUNCTION__" - Can't set cache<%s>.\n",cache_str);
		return false;
	}

	// Sprite manager
	SpriteMngrParams params;
	params.WndHeader=wnd;
	params.FullScreen=OptFullScr;
	params.ScreenWidth=OptScreenWidth;
	params.ScreenHeight=OptScreenHeight;
	params.VSync=OptVSync;
	params.SprFlushVal=OptFlushVal;
	params.BaseTexture=OptBaseTex;
	params.PreRestoreFunc=&_PreRestore;
	params.PostRestoreFunc=&_PostRestore;
	params.DrawOffsetX=&CmnScrOx;
	params.DrawOffsetY=&CmnScrOy;
	params.MultiSampling=OptMultiSampling;
	params.SoftwareSkinning=OptSoftwareSkinning;
	if(!SprMngr.Init(params)) return false;

	// Fonts
	if(!SprMngr.LoadFont(FONT_FO,"fnt_def",1)) return false;
	if(!SprMngr.LoadFont(FONT_NUM,"fnt_num",1)) return false;
	if(!SprMngr.LoadFont(FONT_BIG_NUM,"fnt_big_num",1)) return false;
	if(!SprMngr.LoadFontAAF(FONT_SPECIAL,"font0.aaf",1)) return false;
	if(!SprMngr.LoadFontAAF(FONT_DEF,"font1.aaf",1)) return false;
	if(!SprMngr.LoadFontAAF(FONT_THIN,"font2.aaf",1)) return false;
	if(!SprMngr.LoadFontAAF(FONT_FAT,"font3.aaf",1)) return false;
	if(!SprMngr.LoadFontAAF(FONT_BIG,"font4.aaf",1)) return false;
	SprMngr.SetDefaultFont(FONT_DEF,COLOR_TEXT);

	// Critters sprite manager ptr
	CritterCl::SprMngr=&SprMngr;

	// Names
//	char fonames_path[512];
//	GetPrivateProfileString(CFG_FILE_APP_NAME,"FONamesPath",".\\data\\",fonames_path,sizeof(fonames_path),CLIENT_CONFIG_FILE);
	FONames::GenerateFoNames(PT_CLIENT_DATA);

	// Input
	if(!InitDI()) return false;

	// Resource manager
	ResMngr.Refresh(&SprMngr);

	if(SprMngr.BeginScene(D3DCOLOR_XRGB(100,100,100))) SprMngr.EndScene();

	int res=InitIface();
	if(res!=0)
	{
		WriteLog("Error<%d>.\n",res);
		return false;
	}

	// Language Packs
	char lang_name[5];
	GetPrivateProfileString(CFG_FILE_APP_NAME,"Language",DEFAULT_LANGUAGE,lang_name,5,CLIENT_CONFIG_FILE);
	if(strlen(lang_name)<4) StringCopy(lang_name,DEFAULT_LANGUAGE);

	if(!CurLang.Init(Str::Format("%s%s",FileMngr.GetDataPath(),FileMngr.GetPath(PT_TXT_GAME)),*(DWORD*)&lang_name)) return false;

	MsgText=&CurLang.Msg[TEXTMSG_TEXT];
	MsgDlg=&CurLang.Msg[TEXTMSG_DLG];
	MsgItem=&CurLang.Msg[TEXTMSG_ITEM];
	MsgGame=&CurLang.Msg[TEXTMSG_GAME];
	MsgGM=&CurLang.Msg[TEXTMSG_GM];
	MsgCombat=&CurLang.Msg[TEXTMSG_COMBAT];
	MsgQuest=&CurLang.Msg[TEXTMSG_QUEST];
	MsgHolo=&CurLang.Msg[TEXTMSG_HOLO];
	MsgCraft=&CurLang.Msg[TEXTMSG_CRAFT];

	// Critter types
	CritType::InitFromMsg(&CurLang.Msg[TEXTMSG_INTERNAL]);

	// Item manager
	if(!ItemMngr.Init()) return false;

	// Prototypes
	ItemMngr.ClearProtos();
	DWORD protos_len;
	BYTE* protos=Crypt.GetCache("__item_protos",protos_len);
	if(protos)
	{
		BYTE* protos_uc=Crypt.Uncompress(protos,protos_len,15);
		delete[] protos;
		if(protos_uc)
		{
			ProtoItemVec proto_items;
			proto_items.resize(protos_len/sizeof(ProtoItem));
			memcpy((void*)&proto_items[0],protos_uc,protos_len);
			ItemMngr.ParseProtos(proto_items);
			delete[] protos_uc;
		}
	}

	// Get fast protos
	AddFastProto(SP_GRID_EXITGRID);
	AddFastProto(SP_GRID_ENTIRE);
	AddFastProto(SP_SCEN_LIGHT);
	AddFastProto(SP_SCEN_LIGHT_STOP);
	AddFastProto(SP_SCEN_BLOCK);
	AddFastProto(SP_SCEN_IBLOCK);
	AddFastProto(SP_WALL_BLOCK);
	AddFastProto(SP_WALL_BLOCK_LIGHT);
	AddFastProto(SP_MISC_SCRBLOCK);
	AddFastProto(SP_SCEN_TRIGGER);
	for(int j=SP_MISC_GRID_MAP_BEG;j<=SP_MISC_GRID_MAP_END;j++) AddFastProto(j);
	for(int j=SP_MISC_GRID_GM_BEG;j<=SP_MISC_GRID_GM_END;j++) AddFastProto(j);

	// Fill default critter parameters
	for(int i=0;i<MAPOBJ_CRITTER_PARAMS;i++) DefaultCritterParam[i]=-1;
	DefaultCritterParam[0]=ST_DIALOG_ID;
	DefaultCritterParam[1]=ST_AI_ID;
	DefaultCritterParam[2]=ST_BAG_ID;
	DefaultCritterParam[3]=ST_TEAM_ID;
	DefaultCritterParam[4]=ST_NPC_ROLE;
	DefaultCritterParam[5]=ST_REPLICATION_TIME;

	// Critters Manager
	if(!CrMngr.Init()) return false;
	if(!CrMngr.LoadProtos()) return false;

	for(int i=1;i<MAX_CRIT_PROTOS;i++)
	{
		CritData* data=CrMngr.GetProto(i);
		if(data)
		{
			data->BaseType=data->Params[ST_BASE_CRTYPE];
			NpcProtos.push_back(data);
		}
	}

	// Hex manager
	if(!HexMngr.Init(&SprMngr)) return false;
	if(!HexMngr.ReloadSprites(NULL)) return false;
	HexMngr.SwitchShowTrack();
	DayTime=432720;
	ProcessGameTime();
	AnyId=0x7FFFFFFF;

	if(!OptFullScr)
	{
		RECT r;
		GetWindowRect(Wnd,&r);
		SetCursorPos(r.left+100,r.top+100);
	}
	else 
	{
		SetCursorPos(100,100);
	}

	ShowCursor(FALSE);
	CurX=320;
	CurY=240;
	WriteLog("Mapper initialization complete.\n");

	if(strstr(GetCommandLine(),"-Map"))
	{
		char map_name[256];
		sscanf(strstr(GetCommandLine(),"-Map")+strlen("-Map")+1,"%s",map_name);

		ProtoMap* pmap=new ProtoMap();
		if(pmap->Init(0xFFFF,map_name,PT_MAPS) && HexMngr.SetProtoMap(*pmap))
		{
			HexMngr.FindSetCenter(pmap->Header.CenterX,pmap->Header.CenterY);
			CurProtoMap=pmap;
			LoadedProtoMaps.push_back(pmap);
		}
	}

	// Script system
	InitScriptSystem();

	// Refresh resources after start script executed
	ResMngr.Refresh(NULL);
	RefreshTiles();
	IsMapperStarted=true;

	return true;
}

int FOMapper::InitIface()
{
	WriteLog("Init interface.\n");
	
/************************************************************************/
/* Data                                                                 */
/************************************************************************/
	
	WriteLog("Load data.\n");

	IniParser& ini=IfaceIni;
	char int_file[256];
	char key[256];
	int i;

	GetPrivateProfileString(CFG_FILE_APP_NAME,"Iface_mapper",CFG_DEF_INT_FILE,int_file,256,CLIENT_CONFIG_FILE);
	if(!FileMngr.LoadFile(int_file,PT_ART_INTRFACE))
	{
		WriteLog("File <%d> not found.\n");
		return __LINE__;
	}

	if(!ini.LoadFile(FileMngr.GetBuf(),FileMngr.GetFsize()))
	{
		WriteLog("File load from memory error.\n");
		return __LINE__;
	}

	// Interface
	IntX=ini.GetInt("IntX",-1);
	IntY=ini.GetInt("IntY",-1);

	if(IntX==-1) IntX=(MODE_WIDTH-ini.GetInt("IntWMain2",MODE_WIDTH))/2;
	if(IntY==-1) IntY=MODE_HEIGHT-ini.GetInt("IntWMain3",200);

	for(i=0;i<=3;i++) { sprintf(key,"IntWMain%d",i); IntWMain[i]=ini.GetInt(key,1); }
	for(i=0;i<=3;i++) { sprintf(key,"IntWWork%d",i); IntWWork[i]=ini.GetInt(key,1); }
	for(i=0;i<=3;i++) { sprintf(key,"IntWHint%d",i); IntWHint[i]=ini.GetInt(key,1); }

	for(i=0;i<=3;i++) { sprintf(key,"IntBArm%d",i); IntBArm[i]=ini.GetInt(key,1); }
	for(i=0;i<=3;i++) { sprintf(key,"IntBDrug%d",i); IntBDrug[i]=ini.GetInt(key,1); }
	for(i=0;i<=3;i++) { sprintf(key,"IntBWpn%d",i); IntBWpn[i]=ini.GetInt(key,1); }
	for(i=0;i<=3;i++) { sprintf(key,"IntBAmmo%d",i); IntBAmmo[i]=ini.GetInt(key,1); }
	for(i=0;i<=3;i++) { sprintf(key,"IntBMisc%d",i); IntBMisc[i]=ini.GetInt(key,1); }
	for(i=0;i<=3;i++) { sprintf(key,"IntBMisc2%d",i); IntBMisc2[i]=ini.GetInt(key,1); }
	for(i=0;i<=3;i++) { sprintf(key,"IntBKey%d",i); IntBKey[i]=ini.GetInt(key,1); }
	for(i=0;i<=3;i++) { sprintf(key,"IntBCont%d",i); IntBCont[i]=ini.GetInt(key,1); }
	for(i=0;i<=3;i++) { sprintf(key,"IntBDoor%d",i); IntBDoor[i]=ini.GetInt(key,1); }
	for(i=0;i<=3;i++) { sprintf(key,"IntBGrid%d",i); IntBGrid[i]=ini.GetInt(key,1); }
	for(i=0;i<=3;i++) { sprintf(key,"IntBGen%d",i); IntBGen[i]=ini.GetInt(key,1); }
	for(i=0;i<=3;i++) { sprintf(key,"IntBWall%d",i); IntBWall[i]=ini.GetInt(key,1); }
	for(i=0;i<=3;i++) { sprintf(key,"IntBTile%d",i); IntBTile[i]=ini.GetInt(key,1); }
	for(i=0;i<=3;i++) { sprintf(key,"IntBCrit%d",i); IntBCrit[i]=ini.GetInt(key,1); }
	for(i=0;i<=3;i++) { sprintf(key,"IntBFast%d",i); IntBFast[i]=ini.GetInt(key,1); }
	for(i=0;i<=3;i++) { sprintf(key,"IntBIgnore%d",i); IntBIgnore[i]=ini.GetInt(key,1); }
	for(i=0;i<=3;i++) { sprintf(key,"IntBInCont%d",i); IntBInCont[i]=ini.GetInt(key,1); }
	for(i=0;i<=3;i++) { sprintf(key,"IntBMess%d",i); IntBMess[i]=ini.GetInt(key,1); }
	for(i=0;i<=3;i++) { sprintf(key,"IntBList%d",i); IntBList[i]=ini.GetInt(key,1); }
	for(i=0;i<=3;i++) { sprintf(key,"IntBScrBack%d",i); IntBScrBack[i]=ini.GetInt(key,1); }
	for(i=0;i<=3;i++) { sprintf(key,"IntBScrBackFst%d",i); IntBScrBackFst[i]=ini.GetInt(key,1); }
	for(i=0;i<=3;i++) { sprintf(key,"IntBScrFront%d",i); IntBScrFront[i]=ini.GetInt(key,1); }
	for(i=0;i<=3;i++) { sprintf(key,"IntBScrFrontFst%d",i); IntBScrFrontFst[i]=ini.GetInt(key,1); }

	for(i=0;i<=3;i++) { sprintf(key,"IntBShowItem%d",i); IntBShowItem[i]=ini.GetInt(key,1); }
	for(i=0;i<=3;i++) { sprintf(key,"IntBShowScen%d",i); IntBShowScen[i]=ini.GetInt(key,1); }
	for(i=0;i<=3;i++) { sprintf(key,"IntBShowWall%d",i); IntBShowWall[i]=ini.GetInt(key,1); }
	for(i=0;i<=3;i++) { sprintf(key,"IntBShowCrit%d",i); IntBShowCrit[i]=ini.GetInt(key,1); }
	for(i=0;i<=3;i++) { sprintf(key,"IntBShowTile%d",i); IntBShowTile[i]=ini.GetInt(key,1); }
	for(i=0;i<=3;i++) { sprintf(key,"IntBShowRoof%d",i); IntBShowRoof[i]=ini.GetInt(key,1); }
	for(i=0;i<=3;i++) { sprintf(key,"IntBShowFast%d",i); IntBShowFast[i]=ini.GetInt(key,1); }

	for(i=0;i<=3;i++) { sprintf(key,"IntBSelectItem%d",i); IntBSelectItem[i]=ini.GetInt(key,1); }
	for(i=0;i<=3;i++) { sprintf(key,"IntBSelectScen%d",i); IntBSelectScen[i]=ini.GetInt(key,1); }
	for(i=0;i<=3;i++) { sprintf(key,"IntBSelectWall%d",i); IntBSelectWall[i]=ini.GetInt(key,1); }
	for(i=0;i<=3;i++) { sprintf(key,"IntBSelectCrit%d",i); IntBSelectCrit[i]=ini.GetInt(key,1); }
	for(i=0;i<=3;i++) { sprintf(key,"IntBSelectTile%d",i); IntBSelectTile[i]=ini.GetInt(key,1); }
	for(i=0;i<=3;i++) { sprintf(key,"IntBSelectRoof%d",i); IntBSelectRoof[i]=ini.GetInt(key,1); }

	IntVisible=true;
	IntFix=true;
	IntMode=INT_MODE_MESS;
	IntVectX=0;
	IntVectY=0;
	SelectType=SELECT_TYPE_NEW;

	ScrArm=0;
	ScrDrug=0;
	ScrWpn=0;
	ScrAmmo=0;
	ScrMisc=0;
	ScrMisc2=0;
	ScrKey=0;
	ScrCont=0;
	ScrDoor=0;
	ScrGrid=0;
	ScrGen=0;
	ScrWall=0;
	ScrTile=0;
	ScrCrit=0;
	ScrList=0;

	CurProtoMap=NULL;
	CurItemProtos=NULL;
	CurProtoScroll=NULL;
	ZeroMemory(ProtoScroll,sizeof(ProtoScroll));
	ProtoWidth=ini.GetInt("ProtoWidth",50);
	ProtosOnScreen=(IntWWork[2]-IntWWork[0])/ProtoWidth;
	ZeroMemory(CurProto,sizeof(CurProto));
	NpcDir=3;
	ListScroll=0;

	CurMode=CUR_MODE_DEF;

	SelectHX1=0;
	SelectHY1=0;
	SelectHX2=0;
	SelectHY2=0;

	InContScroll=0;
	InContObject=NULL;

	DrawRoof=false;

	IsSelectItem=true;
	IsSelectScen=true;
	IsSelectWall=true;
	IsSelectCrit=true;
	IsSelectTile=false;
	IsSelectRoof=false;

	OptShowRoof=false;

	// Object
	for(i=0;i<=3;i++) { sprintf(key,"ObjWMain%d",i); ObjWMain[i]=ini.GetInt(key,1); }
	for(i=0;i<=3;i++) { sprintf(key,"ObjWWork%d",i); ObjWWork[i]=ini.GetInt(key,1); }
	for(i=0;i<=3;i++) { sprintf(key,"ObjBToAll%d",i); ObjBToAll[i]=ini.GetInt(key,1); }
	
	ObjX=0;
	ObjY=0;
	ItemVectX=0;
	ItemVectY=0;
	ObjCurLine=0;
	ObjVisible=true;
	ObjFix=false;
	ObjToAll=false;

	// Console
	ConsolePicX=ini.GetInt("ConsolePicX",0);
	ConsolePicY=ini.GetInt("ConsolePicY",0);
	ConsoleTextX=ini.GetInt("ConsoleTextX",0);
	ConsoleTextY=ini.GetInt("ConsoleTextY",0);

	ConsoleEdit=0;
	ConsoleLastKey=0;
	ConsoleKeyTick=0;
	ConsoleAccelerate=0;
	ConsoleStr[0]=0;
	ConsoleCur=0;
	ConsoleHistory.clear();
	ConsoleHistoryCur=0;

	// Messbox
	MessBoxScroll=0;

/************************************************************************/
/* Sprites                                                              */
/************************************************************************/

	ItemHex::DefaultAnim=ResMngr.GetAnim(Str::GetHash("art\\items\\reserved.frm"),0);
	if(!ItemHex::DefaultAnim)
	{
		WriteLog(__FUNCTION__" - Default item animation not found.\n");
		return __LINE__;
	}

	CritterCl::DefaultAnim=SprMngr.LoadAnyAnimation("reservaa.frm",PT_ART_CRITTERS,true,0);
	if(!CritterCl::DefaultAnim)
	{
		WriteLog(__FUNCTION__" - Default critter animation not found.\n");
		return __LINE__;
	}

	char f_name[1024];

	// Cursor
	if(!(CurPDef=SprMngr.LoadSprite("actarrow.frm",PT_ART_INTRFACE))) return __LINE__;
	if(!(CurPHand=SprMngr.LoadSprite("hand.frm",PT_ART_INTRFACE))) return __LINE__;

	// Iface
	ini.GetStr("IntMainPic","error",f_name);
	if(!(IntMainPic=SprMngr.LoadSprite(f_name,PT_ART_INTRFACE))) return __LINE__;
	ini.GetStr("IntPTabPic","error",f_name);
	if(!(IntPTab=SprMngr.LoadSprite(f_name,PT_ART_INTRFACE))) return __LINE__;
	ini.GetStr("IntPSelectPic","error",f_name);
	if(!(IntPSelect=SprMngr.LoadSprite(f_name,PT_ART_INTRFACE))) return __LINE__;
	ini.GetStr("IntPShowPic","error",f_name);
	if(!(IntPShow=SprMngr.LoadSprite(f_name,PT_ART_INTRFACE))) return __LINE__;

	// Object
	ini.GetStr("ObjWMainPic","error",f_name);
	if(!(ObjWMainPic=SprMngr.LoadSprite(f_name,PT_ART_INTRFACE))) return __LINE__;
	ini.GetStr("ObjPBToAllDn","error",f_name);
	if(!(ObjPBToAllDn=SprMngr.LoadSprite(f_name,PT_ART_INTRFACE))) return __LINE__;

	// Console
	ini.GetStr("ConsolePic","error",f_name);
	if(!(ConsolePic=SprMngr.LoadSprite(f_name,PT_ART_INTRFACE))) return __LINE__;

/************************************************************************/
/*                                                                      */
/************************************************************************/

	WriteLog("Init iface success.\n");
	return 0;
}

void FOMapper::Finish()
{
	WriteLog("Mapper finish...\n");
	Keyb::ClearKeyb();
	ResMngr.Finish();
	HexMngr.Clear();
	SprMngr.Clear();
	CrMngr.Finish();
	FileManager::EndOfWork();
	FinishScriptSystem();

	if(Keyboard) Keyboard->Unacquire();
	SAFEREL(Keyboard);
	if(Mouse) Mouse->Unacquire();
	SAFEREL(Mouse);
	SAFEREL(DInput);
	WriteLog("Mapper finish complete.\n");
}

bool FOMapper::InitDI()
{
	WriteLog("DirectInput initialization...\n");

	HRESULT hr=DirectInput8Create(GetModuleHandle(NULL),DIRECTINPUT_VERSION,IID_IDirectInput8,(void**)&DInput,NULL);
	if(hr!=DI_OK)
	{
		WriteLog("Can't create DirectInput.\n");
		return false;
	}

	// Obtain an interface to the system keyboard device.
	hr=DInput->CreateDevice(GUID_SysKeyboard,&Keyboard,NULL);
	if(hr!=DI_OK)
	{
		WriteLog("Can't create GUID_SysKeyboard.\n");
		return false;
	}

	hr=DInput->CreateDevice(GUID_SysMouse,&Mouse,NULL);
	if(hr!=DI_OK)
	{
		WriteLog("Can't create GUID_SysMouse.\n");
		return false;
	}
	// Set the data format to "keyboard format" - a predefined data format 
	// This tells DirectInput that we will be passing an array
	// of 256 bytes to IDirectInputDevice::GetDeviceState.
	hr=Keyboard->SetDataFormat(&c_dfDIKeyboard);
	if(hr!=DI_OK)
	{
		WriteLog("Unable to set data format for keyboard.\n");
		return false;
	}

	hr=Mouse->SetDataFormat(&c_dfDIMouse2);
	if(hr!=DI_OK)
	{
		WriteLog("Unable to set data format for mouse.\n");
		return false;
	}


	hr=Keyboard->SetCooperativeLevel( Wnd,DISCL_FOREGROUND | DISCL_NONEXCLUSIVE);
	if(hr!=DI_OK)
	{
		WriteLog("Unable to set cooperative level for keyboard.\n");
		return false;
	}

	hr=Mouse->SetCooperativeLevel( Wnd,DISCL_FOREGROUND | (OptFullScr?DISCL_EXCLUSIVE:DISCL_NONEXCLUSIVE));
	//	hr=Mouse->SetCooperativeLevel( Wnd,DISCL_FOREGROUND | DISCL_EXCLUSIVE);
	if(hr!=DI_OK)
	{
		WriteLog("Unable to set cooperative level for mouse.\n");
		return false;
	}

	// Important step to use buffered device data!
	DIPROPDWORD dipdw;
	dipdw.diph.dwSize       = sizeof(DIPROPDWORD);
	dipdw.diph.dwHeaderSize = sizeof(DIPROPHEADER);
	dipdw.diph.dwObj        = 0;
	dipdw.diph.dwHow        = DIPH_DEVICE;
	dipdw.dwData            = DI_BUF_SIZE; // Arbitrary buffer size

	hr=Keyboard->SetProperty(DIPROP_BUFFERSIZE,&dipdw.diph);
	if(hr!=DI_OK)
	{
		WriteLog("Unable to set property for keyboard.\n");
		return false;
	}

	hr=Mouse->SetProperty(DIPROP_BUFFERSIZE,&dipdw.diph);
	if(hr!=DI_OK)
	{
		WriteLog("Unable to set property for mouse.\n");
		return false;
	}

	// Acquire the newly created device
	if(Keyboard->Acquire()!=DI_OK) WriteLog("Can't acquire keyboard.\n");
	if(Mouse->Acquire()!=DI_OK) WriteLog("Can't acquire mouse.\n");

	WriteLog("DirectInput initialization complete.\n");
	return true;
}

void FOMapper::ProcessGameTime()
{
	GameOpt.Minute=DayTime%60;
	GameOpt.Hour=DayTime/60%24;
	DWORD color=GetColorDay(HexMngr.GetMapDayTime(),HexMngr.GetMapDayColor(),HexMngr.GetMapTime(),NULL);
	SprMngr.SetSpritesColor(COLOR_GAME_RGB((color>>16)&0xFF,(color>>8)&0xFF,color&0xFF));
	HexMngr.RefreshMap();
}

DWORD FOMapper::AnimLoad(DWORD name_hash, BYTE dir, int res_type)
{
	Self->SprMngr.SurfType=res_type;
	AnyFrames* frm=ResMngr.GetAnim(name_hash,dir);
	Self->SprMngr.SurfType=RES_NONE;
	if(!frm) return 0;
	IfaceAnim* ianim=new IfaceAnim(frm,res_type);
	if(!ianim) return 0;
	size_t index=1;
	for(size_t j=Animations.size();index<j;index++) if(!Animations[index]) break;
	if(index<Animations.size()) Animations[index]=ianim;
	else Animations.push_back(ianim);
	return index;
}

DWORD FOMapper::AnimLoad(const char* fname, int path_type, int res_type)
{
	Self->SprMngr.SurfType=res_type;
	AnyFrames* frm=SprMngr.LoadAnyAnimation(fname,path_type,true,0);
	Self->SprMngr.SurfType=RES_NONE;
	if(!frm) return 0;
	IfaceAnim* ianim=new IfaceAnim(frm,res_type);
	if(!ianim) return 0;
	size_t index=1;
	for(size_t j=Animations.size();index<j;index++) if(!Animations[index]) break;
	if(index<Animations.size()) Animations[index]=ianim;
	else Animations.push_back(ianim);
	return index;
}

DWORD FOMapper::AnimGetCurSpr(DWORD anim_id)
{
	if(anim_id>=Animations.size() || !Animations[anim_id]) return 0;
	return Animations[anim_id]->Frames->Ind[Animations[anim_id]->CurSpr];
}

DWORD FOMapper::AnimGetCurSprCnt(DWORD anim_id)
{
	if(anim_id>=Animations.size() || !Animations[anim_id]) return 0;
	return Animations[anim_id]->CurSpr;
}

DWORD FOMapper::AnimGetSprCount(DWORD anim_id)
{
	if(anim_id>=Animations.size() || !Animations[anim_id]) return 0;
	return Animations[anim_id]->Frames->CntFrm;
}

AnyFrames* FOMapper::AnimGetFrames(DWORD anim_id)
{
	if(anim_id>=Animations.size() || !Animations[anim_id]) return 0;
	return Animations[anim_id]->Frames;
}

void FOMapper::AnimRun(DWORD anim_id, DWORD flags)
{
	if(anim_id>=Animations.size() || !Animations[anim_id]) return;
	IfaceAnim* anim=Animations[anim_id];

	// Set flags
	anim->Flags=flags&0xFFFF;
	flags>>=16;

	// Set frm
	BYTE cur_frm=flags&0xFF;
	if(cur_frm>0)
	{
		cur_frm--;
		if(cur_frm>=anim->Frames->CntFrm) cur_frm=anim->Frames->CntFrm-1;
		anim->CurSpr=cur_frm;
	}
	//flags>>=8;
}

void FOMapper::AnimProcess()
{
	for(IfaceAnimVecIt it=Animations.begin(),end=Animations.end();it!=end;++it)
	{
		IfaceAnim* anim=*it;
		if(!anim || !anim->Flags) continue;

		if(FLAG(anim->Flags,ANIMRUN_STOP))
		{
			anim->Flags=0;
			continue;
		}

		if(FLAG(anim->Flags,ANIMRUN_TO_END) || FLAG(anim->Flags,ANIMRUN_FROM_END))
		{
			DWORD cur_tick=Timer::FastTick();
			if(cur_tick-anim->LastTick<anim->Frames->Ticks/anim->Frames->CntFrm) continue;

			anim->LastTick=cur_tick;
			int end_spr=anim->Frames->CntFrm-1;
			if(FLAG(anim->Flags,ANIMRUN_FROM_END)) end_spr=0;

			if(anim->CurSpr<end_spr) anim->CurSpr++;
			else if(anim->CurSpr>end_spr) anim->CurSpr--;
			else
			{
				if(FLAG(anim->Flags,ANIMRUN_CYCLE))
				{
					if(FLAG(anim->Flags,ANIMRUN_TO_END)) anim->CurSpr=0;
					else anim->CurSpr=end_spr;
				}
				else
				{
					anim->Flags=0;
				}
			}
		}
	}
}

void FOMapper::AnimFree(int res_type)
{
	ResMngr.FreeResources(res_type);
	for(IfaceAnimVecIt it=Animations.begin(),end=Animations.end();it!=end;++it)
	{
		IfaceAnim* anim=*it;
		if(anim && anim->ResType==res_type)
		{
			delete anim;
			(*it)=NULL;
		}
	}
}

void FOMapper::ParseKeyboard()
{
	DWORD elements=DI_BUF_SIZE;
	DIDEVICEOBJECTDATA didod[DI_BUF_SIZE]; // Receives buffered data 
	if(Keyboard->GetDeviceData(sizeof(DIDEVICEOBJECTDATA),didod,&elements,0)!=DI_OK)
	{
		Keyboard->Acquire();
		Keyb::CtrlDwn=false;
		Keyb::AltDwn=false;
		Keyb::ShiftDwn=false;
		if(MapperFunctions.InputLost && Script::PrepareContext(MapperFunctions.InputLost,__FUNCTION__,"Mapper")) Script::RunPrepared();
		return;
	}

	for(int i=0;i<elements;i++) 
	{
		BYTE dikdw=(didod[i].dwData&0x80?didod[i].dwOfs:0);
		BYTE dikup=(!(didod[i].dwData&0x80)?didod[i].dwOfs:0);

		bool script_result=false;
		if(dikdw && MapperFunctions.KeyDown && Script::PrepareContext(MapperFunctions.KeyDown,__FUNCTION__,"Mapper"))
		{
			Script::SetArgByte(dikdw);
			if(Script::RunPrepared()) script_result=Script::GetReturnedBool();
		}
		if(dikup && MapperFunctions.KeyUp && Script::PrepareContext(MapperFunctions.KeyUp,__FUNCTION__,"Mapper"))
		{
			Script::SetArgByte(dikup);
			if(Script::RunPrepared()) script_result=Script::GetReturnedBool();
		}

		if(dikdw==DIK_RCONTROL || dikdw==DIK_LCONTROL)
		{
			Keyb::CtrlDwn=true;
			goto label_TryChangeLang;
		}
		else if(dikdw==DIK_LMENU || dikdw==DIK_RMENU)
		{
			Keyb::AltDwn=true;
			goto label_TryChangeLang;
		}
		else if(dikdw==DIK_LSHIFT || dikdw==DIK_RSHIFT)
		{
			Keyb::ShiftDwn=true;
label_TryChangeLang:
			if(Keyb::ShiftDwn && Keyb::CtrlDwn && OptChangeLang==CHANGE_LANG_CTRL_SHIFT) //Ctrl+Shift
				Keyb::Lang=(Keyb::Lang==LANG_RUS)?LANG_ENG:LANG_RUS;
			if(Keyb::ShiftDwn && Keyb::AltDwn && OptChangeLang==CHANGE_LANG_ALT_SHIFT) //Alt+Shift
				Keyb::Lang=(Keyb::Lang==LANG_RUS)?LANG_ENG:LANG_RUS;
		}
		if(dikup==DIK_RCONTROL || dikup==DIK_LCONTROL) Keyb::CtrlDwn=false;
		else if(dikup==DIK_LMENU || dikup==DIK_RMENU) Keyb::AltDwn=false;
		else if(dikup==DIK_LSHIFT || dikup==DIK_RSHIFT) Keyb::ShiftDwn=false;

		if(script_result || OptDisableKeyboardEvents)
		{
			if(dikdw==DIK_ESCAPE && Keyb::ShiftDwn) CmnQuit=true;
			continue;
		}

		if(!Keyb::AltDwn && !Keyb::CtrlDwn && !Keyb::ShiftDwn)
		{
			switch(dikdw)
			{
			case DIK_F1: OptShowItem=!OptShowItem; HexMngr.RefreshMap(); break;
			case DIK_F2: OptShowScen=!OptShowScen; HexMngr.RefreshMap(); break;
			case DIK_F3: OptShowWall=!OptShowWall; HexMngr.RefreshMap(); break;
			case DIK_F4: OptShowCrit=!OptShowCrit; HexMngr.RefreshMap(); break;
			case DIK_F5: OptShowTile=!OptShowTile; HexMngr.RefreshMap(); break;
			case DIK_F6: OptShowFast=!OptShowFast; HexMngr.RefreshMap(); break;
			case DIK_F7: IntVisible=!IntVisible; break;
			case DIK_F8: OptMouseScroll=!OptMouseScroll; break;
			case DIK_F9: ObjVisible=!ObjVisible; break;
			case DIK_F10: HexMngr.SwitchShowHex(); break;
			case DIK_F11: HexMngr.SwitchShowRain(); break;
			case DIK_DELETE: SelectDelete(); break;
			case DIK_ADD: if(!ConsoleEdit && SelectedObj.empty()) {DayTime+=60; ProcessGameTime();} break;
			case DIK_SUBTRACT: if(!ConsoleEdit && SelectedObj.empty()) {DayTime-=60; ProcessGameTime();} break;
			case DIK_TAB: SelectType=(SelectType==SELECT_TYPE_OLD?SELECT_TYPE_NEW:SELECT_TYPE_OLD); break;
			default: break;
			}
		}

		if(Keyb::ShiftDwn)
		{
			switch(dikdw)
			{
			case DIK_F7: IntFix=!IntFix; break;
			case DIK_F9: ObjFix=!ObjFix; break;
			case DIK_F11: SprMngr.SaveSufaces(); break;
			case DIK_ESCAPE: DestroyWindow(Wnd); break;
			case DIK_ADD: if(!ConsoleEdit && SelectedObj.empty()) {DayTime+=1; ProcessGameTime();} break;
			case DIK_SUBTRACT: if(!ConsoleEdit && SelectedObj.empty()) {DayTime-=1; ProcessGameTime();} break;
			default: break;
			}
		}

		if(Keyb::CtrlDwn)
		{
			switch(dikdw)
			{
			case DIK_X: BufferCut(); break;
			case DIK_C: BufferCopy(); break;
			case DIK_V: BufferPaste(50,50); break;
			case DIK_A: SelectAll(); break;
			case DIK_S: OptScrollCheck=!OptScrollCheck; break;
			case DIK_B: HexMngr.MarkPassedHexes(); break;
			case DIK_M: DrawCrExtInfo++; if(DrawCrExtInfo>DRAW_CR_INFO_MAX) DrawCrExtInfo=0; break;
			case DIK_L: SaveLogFile(); break;
			default: break;
			}
		}

		// Key down
		if(dikdw)
		{
			ConsoleKeyDown(dikdw);

			if(!ConsoleEdit)
			{
				switch(dikdw)
				{
				case DIK_LEFT: CmnDiLeft=true; break;
				case DIK_RIGHT: CmnDiRight=true; break;
				case DIK_UP: CmnDiUp=true; break;
				case DIK_DOWN: CmnDiDown=true; break;
				default: break;
				}

				ObjKeyDown(dikdw);
			}
		}

		// Key up
		if(dikup)
		{
			ConsoleKeyUp(dikup);

			switch(dikup)
			{
			case DIK_LEFT: CmnDiLeft=false; break;
			case DIK_RIGHT: CmnDiRight=false; break;
			case DIK_UP: CmnDiUp=false; break;
			case DIK_DOWN: CmnDiDown=false; break;
			default: break;
			}
		}
	}
}

#define MOUSE_CLICK_LEFT        (0)
#define MOUSE_CLICK_RIGHT       (1)
#define MOUSE_CLICK_MIDDLE      (2)
#define MOUSE_CLICK_WHEEL_UP    (3)
#define MOUSE_CLICK_WHEEL_DOWN  (4)
#define MOUSE_CLICK_EXT0        (5)
#define MOUSE_CLICK_EXT1        (6)
#define MOUSE_CLICK_EXT2        (7)
#define MOUSE_CLICK_EXT3        (8)
#define MOUSE_CLICK_EXT4        (9)
void FOMapper::ParseMouse()
{
	// Windows move cursor in windowed mode
	if(!OptFullScr)
	{
		WINDOWINFO wi;
		wi.cbSize=sizeof(wi);
		GetWindowInfo(Wnd,&wi);
		POINT p;
		GetCursorPos(&p);
		CurX=p.x-wi.rcClient.left;
		CurY=p.y-wi.rcClient.top;

		if(CurX>=MODE_WIDTH) CurX=MODE_WIDTH-1;
		if(CurX<0) CurX=0;
		if(CurY>=MODE_HEIGHT) CurY=MODE_HEIGHT-1;
		if(CurY<0) CurY=0;
	}

	// DirectInput mouse
	DWORD elements=DI_BUF_SIZE;
	DIDEVICEOBJECTDATA didod[DI_BUF_SIZE]; // Receives buffered data 
	if(Mouse->GetDeviceData(sizeof(DIDEVICEOBJECTDATA),didod,&elements,0)!=DI_OK)
	{
		Mouse->Acquire();
		if(MapperFunctions.InputLost && Script::PrepareContext(MapperFunctions.InputLost,__FUNCTION__,"Mapper")) Script::RunPrepared();
		return;
	}

	// Windows move cursor in windowed mode
	if(!OptFullScr)
	{
		static int old_cur_x=CurX;
		static int old_cur_y=CurY;

		if(old_cur_x!=CurX || old_cur_y!=CurY)
		{
			old_cur_x=CurX;
			old_cur_y=CurY;

			IntMouseMove();

			if(MapperFunctions.MouseMove && Script::PrepareContext(MapperFunctions.MouseMove,__FUNCTION__,"Mapper"))
			{
				Script::SetArgDword(CurX);
				Script::SetArgDword(CurY);
				Script::RunPrepared();
			}
		}
	}

	bool ismoved=false;
	for(int i=0;i<elements;i++) 
	{
		// Mouse Move
		// Direct input move cursor in fullscreen mode
		if(OptFullScr)
		{
			DI_ONMOUSE( DIMOFS_X, CurX+=didod[i].dwData*OptMouseSpeed; ismoved=true; continue;);
			DI_ONMOUSE( DIMOFS_Y, CurY+=didod[i].dwData*OptMouseSpeed; ismoved=true; continue;);
		}

		// Scripts
		bool script_result=false;
		DI_ONMOUSE( DIMOFS_Z,
			if(MapperFunctions.MouseDown && Script::PrepareContext(MapperFunctions.MouseDown,__FUNCTION__,"Mapper"))
			{
				Script::SetArgDword(int(didod[i].dwData)>0?MOUSE_CLICK_WHEEL_UP:MOUSE_CLICK_WHEEL_DOWN);
				if(Script::RunPrepared()) script_result=Script::GetReturnedBool();
			}
		);
		DI_ONDOWN( DIMOFS_BUTTON0,
			if(MapperFunctions.MouseDown && Script::PrepareContext(MapperFunctions.MouseDown,__FUNCTION__,"Mapper"))
			{
				Script::SetArgDword(MOUSE_CLICK_LEFT);
				if(Script::RunPrepared()) script_result=Script::GetReturnedBool();
			}
		);
		DI_ONUP( DIMOFS_BUTTON0,
			if(MapperFunctions.MouseUp && Script::PrepareContext(MapperFunctions.MouseUp,__FUNCTION__,"Mapper"))
			{
				Script::SetArgDword(MOUSE_CLICK_LEFT);
				if(Script::RunPrepared()) script_result=Script::GetReturnedBool();
			}
		);
		DI_ONDOWN( DIMOFS_BUTTON1,
			if(MapperFunctions.MouseDown && Script::PrepareContext(MapperFunctions.MouseDown,__FUNCTION__,"Mapper"))
			{
				Script::SetArgDword(MOUSE_CLICK_RIGHT);
				if(Script::RunPrepared()) script_result=Script::GetReturnedBool();
			}
		);
		DI_ONUP( DIMOFS_BUTTON1,
			if(MapperFunctions.MouseUp && Script::PrepareContext(MapperFunctions.MouseUp,__FUNCTION__,"Mapper"))
			{
				Script::SetArgDword(MOUSE_CLICK_RIGHT);
				if(Script::RunPrepared()) script_result=Script::GetReturnedBool();
			}
		);
		DI_ONDOWN( DIMOFS_BUTTON2,
			if(MapperFunctions.MouseDown && Script::PrepareContext(MapperFunctions.MouseDown,__FUNCTION__,"Mapper"))
			{
				Script::SetArgDword(MOUSE_CLICK_MIDDLE);
				if(Script::RunPrepared()) script_result=Script::GetReturnedBool();
			}
		);
		DI_ONUP( DIMOFS_BUTTON2,
			if(MapperFunctions.MouseUp && Script::PrepareContext(MapperFunctions.MouseUp,__FUNCTION__,"Mapper"))
			{
				Script::SetArgDword(MOUSE_CLICK_MIDDLE);
				if(Script::RunPrepared()) script_result=Script::GetReturnedBool();
			}
		);
		DI_ONDOWN( DIMOFS_BUTTON3,
			if(MapperFunctions.MouseDown && Script::PrepareContext(MapperFunctions.MouseDown,__FUNCTION__,"Mapper"))
			{
				Script::SetArgDword(MOUSE_CLICK_EXT0);
				if(Script::RunPrepared()) script_result=Script::GetReturnedBool();
			}
		);
		DI_ONUP( DIMOFS_BUTTON3,
			if(MapperFunctions.MouseUp && Script::PrepareContext(MapperFunctions.MouseUp,__FUNCTION__,"Mapper"))
			{
				Script::SetArgDword(MOUSE_CLICK_EXT0);
				if(Script::RunPrepared()) script_result=Script::GetReturnedBool();
			}
		);
		DI_ONDOWN( DIMOFS_BUTTON4,
			if(MapperFunctions.MouseDown && Script::PrepareContext(MapperFunctions.MouseDown,__FUNCTION__,"Mapper"))
			{
				Script::SetArgDword(MOUSE_CLICK_EXT1);
				if(Script::RunPrepared()) script_result=Script::GetReturnedBool();
			}
		);
		DI_ONUP( DIMOFS_BUTTON4,
			if(MapperFunctions.MouseUp && Script::PrepareContext(MapperFunctions.MouseUp,__FUNCTION__,"Mapper"))
			{
				Script::SetArgDword(MOUSE_CLICK_EXT1);
				if(Script::RunPrepared()) script_result=Script::GetReturnedBool();
			}
		);
		DI_ONDOWN( DIMOFS_BUTTON5,
			if(MapperFunctions.MouseDown && Script::PrepareContext(MapperFunctions.MouseDown,__FUNCTION__,"Mapper"))
			{
				Script::SetArgDword(MOUSE_CLICK_EXT2);
				if(Script::RunPrepared()) script_result=Script::GetReturnedBool();
			}
		);
		DI_ONUP( DIMOFS_BUTTON5,
			if(MapperFunctions.MouseUp && Script::PrepareContext(MapperFunctions.MouseUp,__FUNCTION__,"Mapper"))
			{
				Script::SetArgDword(MOUSE_CLICK_EXT2);
				if(Script::RunPrepared()) script_result=Script::GetReturnedBool();
			}
		);
		DI_ONDOWN( DIMOFS_BUTTON6,
			if(MapperFunctions.MouseDown && Script::PrepareContext(MapperFunctions.MouseDown,__FUNCTION__,"Mapper"))
			{
				Script::SetArgDword(MOUSE_CLICK_EXT3);
				if(Script::RunPrepared()) script_result=Script::GetReturnedBool();
			}
		);
		DI_ONUP( DIMOFS_BUTTON6,
			if(MapperFunctions.MouseUp && Script::PrepareContext(MapperFunctions.MouseUp,__FUNCTION__,"Mapper"))
			{
				Script::SetArgDword(MOUSE_CLICK_EXT3);
				if(Script::RunPrepared()) script_result=Script::GetReturnedBool();
			}
		);
		DI_ONDOWN( DIMOFS_BUTTON7,
			if(MapperFunctions.MouseDown && Script::PrepareContext(MapperFunctions.MouseDown,__FUNCTION__,"Mapper"))
			{
				Script::SetArgDword(MOUSE_CLICK_EXT4);
				if(Script::RunPrepared()) script_result=Script::GetReturnedBool();
			}
		);
		DI_ONUP( DIMOFS_BUTTON7,
			if(MapperFunctions.MouseUp && Script::PrepareContext(MapperFunctions.MouseUp,__FUNCTION__,"Mapper"))
			{
				Script::SetArgDword(MOUSE_CLICK_EXT4);
				if(Script::RunPrepared()) script_result=Script::GetReturnedBool();
			}
		);
		if(script_result || OptDisableMouseEvents) continue;

		// Wheel
		DI_ONMOUSE( DIMOFS_Z,
			if(IntVisible && IsCurInRect(IntWWork,IntX,IntY) && (IsObjectMode() || IsTileMode() || IsCritMode()))
			{
				int step=1;
				if(Keyb::ShiftDwn) step=ProtosOnScreen;
				else if(Keyb::CtrlDwn) step=100;
				else if(Keyb::AltDwn) step=1000;

				int data=didod[i].dwData;
				if(data>0)
				{
					if(IsObjectMode() || IsTileMode() || IsCritMode())
					{
						(*CurProtoScroll)-=step;
						if(*CurProtoScroll<0) *CurProtoScroll=0;
					}
					else if(IntMode==INT_MODE_INCONT)
					{
						InContScroll-=step;
						if(InContScroll<0) InContScroll=0;
					}
					else if(IntMode==INT_MODE_LIST)
					{
						ListScroll-=step;
						if(ListScroll<0) ListScroll=0;
					}
				}
				else
				{
					if(IsObjectMode() && (*CurItemProtos).size())
					{
						(*CurProtoScroll)+=step;
						if(*CurProtoScroll>=(*CurItemProtos).size()) *CurProtoScroll=(*CurItemProtos).size()-1;
					}
					else if(IsTileMode() && TilesPictures.size())
					{
						(*CurProtoScroll)+=step;
						if(*CurProtoScroll>=TilesPictures.size()) *CurProtoScroll=TilesPictures.size()-1;
					}
					else if(IsCritMode() && NpcProtos.size())
					{
						(*CurProtoScroll)+=step;
						if(*CurProtoScroll>=NpcProtos.size()) *CurProtoScroll=NpcProtos.size()-1;
					}
					else if(IntMode==INT_MODE_INCONT) InContScroll+=step;
					else if(IntMode==INT_MODE_LIST) ListScroll+=step;
				}
			}
			else
			{
				if(didod[i].dwData) HexMngr.ChangeZoom((int)didod[i].dwData>0?-1:1);
			}
			continue;
			);

		// Middle down
		DI_ONDOWN( DIMOFS_BUTTON2,
			CurMMouseDown();
			continue;
			);

		// Left Button Down
		DI_ONDOWN( DIMOFS_BUTTON0,
			IntLMouseDown();
			continue;	
		);

		// Left Button Up
		DI_ONUP( DIMOFS_BUTTON0,
			IntLMouseUp();
			continue;
		);

		// Right Button Down
		DI_ONDOWN( DIMOFS_BUTTON1,
			continue;
		);

		// Right Button Up
		DI_ONUP( DIMOFS_BUTTON1,
			CurRMouseUp();
			continue;
		);
	}

	// Direct input move cursor in fulscreen mode
	if(ismoved)
	{
		if(CurX>=MODE_WIDTH) CurX=MODE_WIDTH;
		if(CurX<0) CurX=0;
		if(CurY>=MODE_HEIGHT) CurY=MODE_HEIGHT;
		if(CurY<0) CurY=0;

		IntMouseMove();

		if(MapperFunctions.MouseMove && Script::PrepareContext(MapperFunctions.MouseMove,__FUNCTION__,"Mapper"))
		{
			Script::SetArgDword(CurX);
			Script::SetArgDword(CurY);
			Script::RunPrepared();
		}
	}

	// Scroll
	if(OptMouseScroll==false) return;

	if(CurX>=MODE_WIDTH-1)
		CmnDiMright=true;
	else
		CmnDiMright=false;

	if(CurX<=0)
		CmnDiMleft=true;
	else
		CmnDiMleft=false;

	if(CurY>=MODE_HEIGHT-1)
		CmnDiMdown=true;
	else
		CmnDiMdown=false;

	if(CurY<=0)
		CmnDiMup=true;
	else
		CmnDiMup=false;
}

void FOMapper::MainLoop()
{
	// Count FPS
	static DWORD last_call=Timer::FastTick();
	static WORD call_cnt=0;

    if((Timer::FastTick()-last_call)>=1000)
	{
		FPS=call_cnt;
		call_cnt=0;
		last_call=Timer::FastTick();
	}
	else call_cnt++;

	// Script loop
	static DWORD next_call=0;
	if(MapperFunctions.Loop && Timer::FastTick()>=next_call)
	{
		DWORD wait_tick=60000;
		if(Script::PrepareContext(MapperFunctions.Loop,__FUNCTION__,"Mapper") && Script::RunPrepared()) wait_tick=Script::GetReturnedDword();
		next_call=Timer::FastTick()+wait_tick;
	}

	// Input
	ConsoleProcess();
	ParseKeyboard();
	ParseMouse();

	// Process
	AnimProcess();

	if(HexMngr.IsMapLoaded())
	{
		for(CritMapIt it=HexMngr.allCritters.begin(),end=HexMngr.allCritters.end();it!=end;++it)
		{
			CritterCl* cr=(*it).second;
			cr->Process();

		//	if(cr->IsNeedReSet())
		//	{
		//		HexMngr.RemoveCrit(cr);
		//		HexMngr.SetCrit(cr);
		//		cr->ReSetOk();
		//	}

			if(cr->IsNeedMove())
			{
				bool err_move=((!cr->IsRunning && !CritType::IsCanWalk(cr->GetCrType())) || (cr->IsRunning && !CritType::IsCanRun(cr->GetCrType())));
				WORD old_hx=cr->GetHexX();
				WORD old_hy=cr->GetHexY();
				MapObject* mobj=FindMapObject(old_hx,old_hy,MAP_OBJECT_CRITTER,cr->Flags,false);
				if(!err_move && mobj && HexMngr.TransitCritter(cr,cr->MoveSteps[0].first,cr->MoveSteps[0].second,true,false))
				{
					cr->MoveSteps.erase(cr->MoveSteps.begin());
					mobj->MapX=cr->GetHexX();
					mobj->MapY=cr->GetHexY();
					mobj->Dir=cr->GetDir();

					// Move In container items
					for(int i=0,j=CurProtoMap->MObjects.size();i<j;i++)
					{
						MapObject* mo=CurProtoMap->MObjects[i];
						if(mo->MapObjType==MAP_OBJECT_ITEM && mo->MItem.InContainer && mo->MapX==old_hx && mo->MapY==old_hy)
						{
							mo->MapX=cr->GetHexX();
							mo->MapY=cr->GetHexY();
						}
					}
				}
				else
				{
					cr->MoveSteps.clear();
				}

				HexMngr.RebuildLight();
			}
		}

		HexMngr.Scroll();
		HexMngr.ProcessItems();
		HexMngr.ProcessRain();
	}

	// Render
	if(!SprMngr.BeginScene(D3DCOLOR_XRGB(100,100,100)))
	{
		Sleep(100);
		return;
	}

	DrawIfaceLayer(0);
	if(HexMngr.IsMapLoaded())
	{
		HexMngr.DrawMap();
		SprMngr.Flush();

		// Texts on heads
		if(DrawCrExtInfo)
		{
			for(CritMapIt it=HexMngr.allCritters.begin(),end=HexMngr.allCritters.end();it!=end;++it)
			{
				CritterCl* cr=(*it).second;
				if(cr->SprDrawValid)
				{
					MapObject* mobj=FindMapObject(cr->GetHexX(),cr->GetHexY(),MAP_OBJECT_CRITTER,cr->Flags,false);
					CritData* pnpc=CrMngr.GetProto(mobj->ProtoId);
					if(!mobj || !pnpc) continue;
					char str[512]={0};

					if(DrawCrExtInfo==1) sprintf(str,"|0xffaabbcc ProtoId...%u\n|0xffff1122 DialogId...%u\n|0xff4433ff BagId...%u\n|0xff55ff77 TeamId...%u\n",mobj->ProtoId,cr->Params[ST_DIALOG_ID],cr->Params[ST_BAG_ID],cr->Params[ST_TEAM_ID]);
					else if(DrawCrExtInfo==2) sprintf(str,"|0xff11ff22 NpcRole...%u\n|0xffccaabb AiPacket...%u (%u)\n|0xffff00ff RespawnTime...%d\n",cr->Params[ST_NPC_ROLE],cr->Params[ST_AI_ID],pnpc->Params[ST_AI_ID],cr->Params[ST_REPLICATION_TIME]);
					else if(DrawCrExtInfo==3) sprintf(str,"|0xff00ff00 ScriptName...%s\n|0xffff0000 FuncName...%s\n",mobj->ScriptName,mobj->FuncName);

					cr->SetText(str,COLOR_TEXT_WHITE,60000000);
					cr->DrawTextOnHead();
				}
			}
		}

		// Texts on map
		DWORD tick=Timer::FastTick();
		for(MapTextVecIt it=GameMapTexts.begin();it!=GameMapTexts.end();)
		{
			MapText& t=(*it);
			if(tick>=t.StartTick+t.Tick) it=GameMapTexts.erase(it);
			else
			{
				int procent=Procent(t.Tick,tick-t.StartTick);
				INTRECT r=AverageFlexRect(t.Rect,t.EndRect,procent);
				Field& f=HexMngr.GetField(t.HexX,t.HexY);
				int x=(f.ScrX+32/2+CmnScrOx)/SpritesZoom-100-(t.Rect.L-r.L);
				int y=(f.ScrY+12/2-t.Rect.H()-(t.Rect.T-r.T)+CmnScrOy)/SpritesZoom-70;
				DWORD color=t.Color;
				if(t.Fade) color=(color^0xFF000000)|((0xFF*(100-procent)/100)<<24);
				SprMngr.DrawStr(INTRECT(x,y,x+200,y+70),t.Text.c_str(),FT_CENTERX|FT_BOTTOM|FT_COLORIZE|FT_BORDERED,color);
				it++;
			}
		}
	}

	// Iface
	DrawIfaceLayer(1);
	IntDraw();
	DrawIfaceLayer(2);
	ConsoleDraw();
	DrawIfaceLayer(3);
	ObjDraw();
	DrawIfaceLayer(4);
	CurDraw();
	DrawIfaceLayer(5);
	SprMngr.EndScene();
}

void FOMapper::AddFastProto(WORD pid)
{
	ProtoItem* proto_item=ItemMngr.GetProtoItem(pid);
	if(!proto_item) return;
	FastItemProto.push_back(*proto_item);
	HexMngr.AddFastPid(pid);
}

bool StringCompare(const string &left, const string &right)
{
	for(string::const_iterator lit=left.begin(),rit=right.begin();lit!=left.end() && rit!=right.end();++lit,++rit)
	{
		int lc=tolower(*lit);
		int rc=tolower(*rit);
		if(lc<rc) return true;
		else if(lc>rc) return false;
	}
	return left.size()<right.size();
}

void FOMapper::RefreshTiles()
{
	StrVec formats;
	formats.push_back("frm");
	formats.push_back("fofrm");
	formats.push_back("bmp");
	formats.push_back("dds");
	formats.push_back("dib");
	formats.push_back("hdr");
	formats.push_back("jpg");
	formats.push_back("jpeg");
	formats.push_back("pfm");
	formats.push_back("png");
	formats.push_back("ppm");
	formats.push_back("tga");
	formats.push_back("x");
	formats.push_back("3ds");
	formats.push_back("fo3d");

	StrVec tiles;
	FileManager::GetFolderFileNames(PT_ART_TILES,NULL,tiles);
	FileManager::GetDatsFileNames(PT_ART_TILES,NULL,tiles);
	TilesPictures.clear();
	TilesPicturesNames.clear();
	TilesPictures.reserve(tiles.size());
	TilesPicturesNames.reserve(tiles.size());
	std::sort(tiles.begin(),tiles.end(),StringCompare);

	for(StrVecIt it=tiles.begin(),end=tiles.end();it!=end;++it)
	{
		const string& str=*it;
		const char* ext=FileManager::GetExtension(str.c_str());
		if(ext && std::find(formats.begin(),formats.end(),ext)!=formats.end())
		{
			DWORD hash=Str::GetHash(str.c_str());
			if(std::find(TilesPictures.begin(),TilesPictures.end(),hash)==TilesPictures.end())
			{
				TilesPictures.push_back(hash);
				size_t pos=str.find_last_of('\\');
				if(pos!=string::npos) TilesPicturesNames.push_back(str.substr(pos+1));
				else TilesPicturesNames.push_back(str);
			}
		}
	}
}

void FOMapper::IntDraw()
{
	if(!IntVisible) return;

	SprMngr.DrawSprite(IntMainPic,IntX,IntY);

	switch(IntMode)
	{
	case INT_MODE_NONE: break;
	case INT_MODE_ARMOR: SprMngr.DrawSprite(IntPTab,IntBArm[0]+IntX,IntBArm[1]+IntY); break;
	case INT_MODE_DRUG: SprMngr.DrawSprite(IntPTab,IntBDrug[0]+IntX,IntBDrug[1]+IntY); break;
	case INT_MODE_WEAPON: SprMngr.DrawSprite(IntPTab,IntBWpn[0]+IntX,IntBWpn[1]+IntY); break;
	case INT_MODE_AMMO: SprMngr.DrawSprite(IntPTab,IntBAmmo[0]+IntX,IntBAmmo[1]+IntY); break;
	case INT_MODE_MISC: SprMngr.DrawSprite(IntPTab,IntBMisc[0]+IntX,IntBMisc[1]+IntY); break;
	case INT_MODE_MISC2: SprMngr.DrawSprite(IntPTab,IntBMisc2[0]+IntX,IntBMisc2[1]+IntY); break;
	case INT_MODE_KEY: SprMngr.DrawSprite(IntPTab,IntBKey[0]+IntX,IntBKey[1]+IntY); break;
	case INT_MODE_CONT: SprMngr.DrawSprite(IntPTab,IntBCont[0]+IntX,IntBCont[1]+IntY); break;
	case INT_MODE_DOOR: SprMngr.DrawSprite(IntPTab,IntBDoor[0]+IntX,IntBDoor[1]+IntY); break;
	case INT_MODE_GRID: SprMngr.DrawSprite(IntPTab,IntBGrid[0]+IntX,IntBGrid[1]+IntY); break;
	case INT_MODE_GENERIC: SprMngr.DrawSprite(IntPTab,IntBGen[0]+IntX,IntBGen[1]+IntY); break;
	case INT_MODE_WALL: SprMngr.DrawSprite(IntPTab,IntBWall[0]+IntX,IntBWall[1]+IntY); break;
	case INT_MODE_TILE: SprMngr.DrawSprite(IntPTab,IntBTile[0]+IntX,IntBTile[1]+IntY); break;
	case INT_MODE_CRIT: SprMngr.DrawSprite(IntPTab,IntBCrit[0]+IntX,IntBCrit[1]+IntY); break;
	case INT_MODE_FAST: SprMngr.DrawSprite(IntPTab,IntBFast[0]+IntX,IntBFast[1]+IntY); break;
	case INT_MODE_IGNORE: SprMngr.DrawSprite(IntPTab,IntBIgnore[0]+IntX,IntBIgnore[1]+IntY); break;
	case INT_MODE_INCONT: SprMngr.DrawSprite(IntPTab,IntBInCont[0]+IntX,IntBInCont[1]+IntY); break;
	case INT_MODE_MESS: SprMngr.DrawSprite(IntPTab,IntBMess[0]+IntX,IntBMess[1]+IntY); break;	
	case INT_MODE_LIST: SprMngr.DrawSprite(IntPTab,IntBList[0]+IntX,IntBList[1]+IntY); break;
	default: break;
	}

	if(OptShowItem) SprMngr.DrawSprite(IntPShow,IntBShowItem[0]+IntX,IntBShowItem[1]+IntY);
	if(OptShowScen) SprMngr.DrawSprite(IntPShow,IntBShowScen[0]+IntX,IntBShowScen[1]+IntY);
	if(OptShowWall) SprMngr.DrawSprite(IntPShow,IntBShowWall[0]+IntX,IntBShowWall[1]+IntY);
	if(OptShowCrit) SprMngr.DrawSprite(IntPShow,IntBShowCrit[0]+IntX,IntBShowCrit[1]+IntY);
	if(OptShowTile) SprMngr.DrawSprite(IntPShow,IntBShowTile[0]+IntX,IntBShowTile[1]+IntY);
	if(OptShowRoof) SprMngr.DrawSprite(IntPShow,IntBShowRoof[0]+IntX,IntBShowRoof[1]+IntY);
	if(OptShowFast) SprMngr.DrawSprite(IntPShow,IntBShowFast[0]+IntX,IntBShowFast[1]+IntY);

	if(IsSelectItem) SprMngr.DrawSprite(IntPSelect,IntBSelectItem[0]+IntX,IntBSelectItem[1]+IntY);
	if(IsSelectScen) SprMngr.DrawSprite(IntPSelect,IntBSelectScen[0]+IntX,IntBSelectScen[1]+IntY);
	if(IsSelectWall) SprMngr.DrawSprite(IntPSelect,IntBSelectWall[0]+IntX,IntBSelectWall[1]+IntY);
	if(IsSelectCrit) SprMngr.DrawSprite(IntPSelect,IntBSelectCrit[0]+IntX,IntBSelectCrit[1]+IntY);
	if(IsSelectTile) SprMngr.DrawSprite(IntPSelect,IntBSelectTile[0]+IntX,IntBSelectTile[1]+IntY);
	if(IsSelectRoof) SprMngr.DrawSprite(IntPSelect,IntBSelectRoof[0]+IntX,IntBSelectRoof[1]+IntY);

	int x=IntWWork[0]+IntX;
	int y=IntWWork[1]+IntY;
	int h=IntWWork[3]-IntWWork[1];
	int w=ProtoWidth;

	if(IsObjectMode())
	{
		int i=*CurProtoScroll;
		int j=i+ProtosOnScreen;
		if(j>(*CurItemProtos).size()) j=(*CurItemProtos).size();

		for(;i<j;i++,x+=w)
		{
			ProtoItem* proto_item=&(*CurItemProtos)[i];
			DWORD spr_id=ResMngr.GetSprId(proto_item->PicMapHash,proto_item->Dir);
			if(!spr_id) spr_id=ItemHex::DefaultAnim->GetSprId(0);

			DWORD col=(i==CurProto[IntMode]?COLOR_IFACE_RED:COLOR_IFACE);
			SprMngr.DrawSpriteSize(spr_id,x,y,w,h/2,false,true,col);

			if(proto_item->IsItem())
			{
				AnyFrames* anim=ResMngr.GetInvAnim(proto_item->PicInvHash);
				if(anim) SprMngr.DrawSpriteSize(anim->GetCurSprId(),x,y+h/2,w,h/2,false,true,col);
			}

			SprMngr.DrawStr(INTRECT(x,y+h-15,x+w,y+h),Str::Format("%u",proto_item->GetPid()),FT_NOBREAK,COLOR_TEXT_WHITE);

			if(i==CurProto[IntMode])
			{
				string info=MsgItem->GetStr(proto_item->GetInfo());
				info+=" - ";
				info+=MsgItem->GetStr(proto_item->GetInfo()+1);
				SprMngr.DrawStr(INTRECT(IntWHint,IntX,IntY),info.c_str(),FT_COLORIZE);
			}
		}
	}
	else if(IsTileMode())
	{
		int i=*CurProtoScroll;
		int j=i+ProtosOnScreen;
		if(j>TilesPictures.size()) j=TilesPictures.size();

		for(;i<j;i++,x+=w)
		{
			DWORD spr_id=ResMngr.GetSprId(TilesPictures[i],0);
			if(!spr_id) spr_id=ItemHex::DefaultAnim->GetSprId(0);

			DWORD col=(i==CurProto[IntMode]?COLOR_IFACE_RED:COLOR_IFACE);
			SprMngr.DrawSpriteSize(spr_id,x,y,w,h/2,false,true,col);
			SprMngr.DrawStr(INTRECT(x,y+h-15,x+w,y+h),TilesPicturesNames[i].c_str(),FT_NOBREAK,COLOR_TEXT_WHITE);
		}
	}
	else if(IsCritMode())
	{
		int i=*CurProtoScroll;
		int j=i+ProtosOnScreen;
		if(j>NpcProtos.size()) j=NpcProtos.size();

		for(;i<j;i++,x+=w)
		{
			CritData* pnpc=NpcProtos[i];

			AnyFrames* anim=ResMngr.GetCritAnim(pnpc->BaseType,1,1,NpcDir);
			DWORD spr_id=(anim?anim->GetSprId(0):0);
			if(!spr_id) continue;

			DWORD col=COLOR_IFACE;
			if(i==CurProto[IntMode]) col=COLOR_IFACE_RED;

			SprMngr.DrawSpriteSize(spr_id,x,y,w,h/2,false,true,col);
			SprMngr.DrawStr(INTRECT(x,y+h-15,x+w,y+h),Str::Format("%u",pnpc->ProtoId),FT_NOBREAK,COLOR_TEXT_WHITE);

// 			if(i==CurProto[IntMode])
// 			{
// 				sprintf(str,"%s",MsgItem->GetStr(proto_item->GetInfoId()));
// 				FntMngr.DrawStr(INTRECT(IntWHint,IntX,IntY),str,FT_COLORIZE);
// 			}
		}
	}
	else if(IntMode==INT_MODE_INCONT && !SelectedObj.empty())
	{
		SelMapObj* o=&SelectedObj[0];

		int i=InContScroll;
		int j=i+ProtosOnScreen;
		if(j>o->Childs.size()) j=o->Childs.size();

		for(;i<j;i++,x+=w)
		{
			MapObject* mobj=o->Childs[i];
			ProtoItem* proto_item=ItemMngr.GetProtoItem(mobj->ProtoId);
			if(!proto_item) continue;

			AnyFrames* anim=ResMngr.GetInvAnim(proto_item->PicInvHash);
			if(!anim) continue;

			DWORD col=COLOR_IFACE;
			if(mobj==InContObject) col=COLOR_IFACE_RED;

			SprMngr.DrawSpriteSize(anim->GetCurSprId(),x,y,w,h,false,true,col);
			SprMngr.Flush();

			DWORD cnt=(proto_item->IsGrouped()?mobj->MItem.Count:1);
			if(proto_item->GetType()==ITEM_TYPE_AMMO && !cnt) cnt=proto_item->Ammo.StartCount;
			if(!cnt) cnt=1;
			SprMngr.DrawStr(INTRECT(x,y+h-15,x+w,y+h),Str::Format("x%u",cnt),FT_NOBREAK,COLOR_TEXT_WHITE);
			if(mobj->MItem.ItemSlot!=SLOT_INV)
			{
				if(mobj->MItem.ItemSlot==SLOT_HAND1) SprMngr.DrawStr(INTRECT(x,y,x+w,y+h),"Main",FT_NOBREAK,COLOR_TEXT_WHITE);
				else if(mobj->MItem.ItemSlot==SLOT_HAND2) SprMngr.DrawStr(INTRECT(x,y,x+w,y+h),"Ext",FT_NOBREAK,COLOR_TEXT_WHITE);
				else if(mobj->MItem.ItemSlot==SLOT_ARMOR) SprMngr.DrawStr(INTRECT(x,y,x+w,y+h),"Armor",FT_NOBREAK,COLOR_TEXT_WHITE);
				else SprMngr.DrawStr(INTRECT(x,y,x+w,y+h),"Error",FT_NOBREAK,COLOR_TEXT_WHITE);
			}
		}
	}
	else if(IntMode==INT_MODE_LIST)
	{
		SprMngr.Flush();

		int i=ListScroll;
		int j=LoadedProtoMaps.size();

		for(;i<j;i++,x+=w)
		{
			ProtoMap* pm=LoadedProtoMaps[i];
			SprMngr.DrawStr(INTRECT(x,y,x+w,y+h),Str::Format("<%s>",pm->GetName()),0,pm==CurProtoMap?COLOR_IFACE_RED:COLOR_TEXT);
		}
	}

	SprMngr.Flush();

	switch(IntMode)
	{
	case INT_MODE_NONE: break;
	case INT_MODE_MESS: MessBoxDraw(); break;	
	default: break;
	}

	WORD hx,hy;
	if(HexMngr.GetHexPixel(CurX,CurY,hx,hy))
	{
		char str[256];
		sprintf(str,"hx<%u>\nhy<%u>\ntime<%u:%u>\nfps<%u>\n%s",hx,hy,DayTime/60%24,DayTime%60,FPS,OptScrollCheck?"ScrollCheck":"");
		SprMngr.DrawStr(INTRECT(IntWMain,IntX+3,IntY+2),str,FT_NOBREAK);
	}
}

void FOMapper::ObjDraw()
{
	if(!ObjVisible) return;
	if(SelectedObj.empty()) return;

	SelMapObj& so=SelectedObj[0];
	MapObject* o=SelectedObj[0].MapObj;
	if(IntMode==INT_MODE_INCONT && InContObject) o=InContObject;

	ProtoItem* proto=NULL;
	if(o->MapObjType!=MAP_OBJECT_CRITTER) proto=ItemMngr.GetProtoItem(o->ProtoId);

	int w=ObjWWork[2]-ObjWWork[0];
	int h=ObjWWork[3]-ObjWWork[1];
	int x=ObjWWork[0]+ObjX;
	int y=ObjWWork[1]+ObjY;

	SprMngr.DrawSprite(ObjWMainPic,ObjX,ObjY);
	if(ObjToAll) SprMngr.DrawSprite(ObjPBToAllDn,ObjBToAll[0]+ObjX,ObjBToAll[1]+ObjY);

	if(proto)
	{
		DWORD spr_id=ResMngr.GetSprId(o->MItem.PicMapHash?o->MItem.PicMapHash:proto->PicMapHash,o->Dir);
		if(!spr_id) spr_id=ItemHex::DefaultAnim->GetSprId(0);
		SprMngr.DrawSpriteSize(spr_id,x+w-ProtoWidth,y,ProtoWidth,ProtoWidth,false,true);

		if(proto->IsItem())
		{
			AnyFrames* anim=ResMngr.GetInvAnim(o->MItem.PicInvHash?o->MItem.PicInvHash:proto->PicInvHash);
			if(anim) SprMngr.DrawSpriteSize(anim->GetCurSprId(),x+w-ProtoWidth,y+ProtoWidth,ProtoWidth,ProtoWidth,false,true);
		}
	}

	SprMngr.Flush();

	int step=DRAW_NEXT_HEIGHT;
	DWORD col=COLOR_TEXT;
	int dummy=0;

//====================================================================================
#define DRAW_COMPONENT(name,val,unsign,cnst) \
	do{\
		char str_[256];\
		col=COLOR_TEXT;\
		if(ObjCurLine==(y-ObjWWork[1]-ObjY)/DRAW_NEXT_HEIGHT) col=COLOR_TEXT_RED;\
		if((cnst)==true) col=COLOR_TEXT_WHITE;\
		StringCopy(str_,name);\
		StringAppend(str_,"....................................................");\
		SprMngr.DrawStr(INTRECT(INTRECT(x,y,x+w/3,y+h),0,0),str_,FT_NOBREAK,col);\
		(unsign==true)?sprintf(str_,"%u (0x%0X)",val,val):sprintf(str_,"%d (0x%0X)",val,val);\
		SprMngr.DrawStr(INTRECT(INTRECT(x+w/3,y,x+w,y+h),0,0),str_,FT_NOBREAK,col);\
		y+=step;\
	}while(0)
#define DRAW_COMPONENT_TEXT(name,text,cnst) \
	do{\
		char str_[256];\
		col=COLOR_TEXT;\
		if(ObjCurLine==(y-ObjWWork[1]-ObjY)/DRAW_NEXT_HEIGHT) col=COLOR_TEXT_RED;\
		if((cnst)==true) col=COLOR_TEXT_WHITE;\
		StringCopy(str_,name);\
		StringAppend(str_,"....................................................");\
		SprMngr.DrawStr(INTRECT(INTRECT(x,y,x+w/3,y+h),0,0),str_,FT_NOBREAK,col);\
		StringCopy(str_,text?text:"");\
		SprMngr.DrawStr(INTRECT(INTRECT(x+w/3,y,x+w,y+h),0,0),str_,FT_NOBREAK,col);\
		y+=step;\
	}while(0)
//====================================================================================

	if(so.MapNpc)
	{
		DRAW_COMPONENT_TEXT("Name",CritType::GetName(so.MapNpc->GetCrType()),true);     // 0
		DRAW_COMPONENT_TEXT("Type",Str::Format("Type %u",so.MapNpc->GetCrType()),true); // 1
	}
	else if(so.MapItem && proto)
	{
		DRAW_COMPONENT_TEXT("PicMap",ResMngr.GetName(proto->PicMapHash),true);          // 0
		DRAW_COMPONENT_TEXT("PicInv",ResMngr.GetName(proto->PicInvHash),true);          // 1
	}

	if(o->MapObjType==MAP_OBJECT_CRITTER) DRAW_COMPONENT_TEXT("MapObjType","Critter",true); // 2
	if(o->MapObjType==MAP_OBJECT_ITEM) DRAW_COMPONENT_TEXT("MapObjType","Item",true);       // 2
	if(o->MapObjType==MAP_OBJECT_SCENERY) DRAW_COMPONENT_TEXT("MapObjType","Scenery",true); // 2
	DRAW_COMPONENT("ProtoId",o->ProtoId,true,true);                                     // 3
	DRAW_COMPONENT("MapX",o->MapX,true,true);                                           // 4
	DRAW_COMPONENT("MapY",o->MapY,true,true);                                           // 5
	DRAW_COMPONENT("Dir",o->Dir,false,o->MapObjType==MAP_OBJECT_CRITTER);               // 6
	DRAW_COMPONENT_TEXT("ScriptName",o->ScriptName,false);                              // 7
	DRAW_COMPONENT_TEXT("FuncName",o->FuncName,false);                                  // 8
	DRAW_COMPONENT("LightIntensity",o->LightIntensity,false,false);                     // 9
	DRAW_COMPONENT("LightDistance",o->LightDistance,true,false);                        // 10
	DRAW_COMPONENT("LightColor",o->LightColor,true,false);                              // 11
	DRAW_COMPONENT("LightDirOff",o->LightDirOff,true,false);                            // 12
	DRAW_COMPONENT("LightDay",o->LightDay,true,false);                                  // 13

	if(o->MapObjType==MAP_OBJECT_CRITTER)
	{
		DRAW_COMPONENT("Cond",o->MCritter.Cond,true,false);                             // 14
		DRAW_COMPONENT("CondExt",o->MCritter.CondExt,true,false);                       // 15
		for(int i=0;i<MAPOBJ_CRITTER_PARAMS;i++)                                        // 16..30
		{
			if(o->MCritter.ParamIndex[i]>=0 && o->MCritter.ParamIndex[i]<MAX_PARAMS)
			{
				const char* param_name=FONames::GetParamName(o->MCritter.ParamIndex[i]);
				DRAW_COMPONENT(param_name?param_name:"???",o->MCritter.ParamValue[i],false,false);
			}
		}
	}
	else if(o->MapObjType==MAP_OBJECT_ITEM || o->MapObjType==MAP_OBJECT_SCENERY)
	{
		DRAW_COMPONENT("OffsetX",o->MItem.OffsetX,false,false);                          // 14
		DRAW_COMPONENT("OffsetY",o->MItem.OffsetY,false,false);                          // 15
		DRAW_COMPONENT("AnimStayBegin",o->MItem.AnimStayBegin,true,false);               // 16
		DRAW_COMPONENT("AnimStayEnd",o->MItem.AnimStayEnd,true,false);                   // 17
		DRAW_COMPONENT("AnimWaitTime",o->MItem.AnimWait,true,false);                     // 18
		DRAW_COMPONENT_TEXT("PicMap",o->RunTime.PicMapName,false);                       // 19
		DRAW_COMPONENT_TEXT("PicInv",o->RunTime.PicInvName,false);                       // 20
		DRAW_COMPONENT("InfoOffset",o->MItem.InfoOffset,true,false);                     // 21

		if(o->MapObjType==MAP_OBJECT_ITEM)
		{
			DRAW_COMPONENT("TrapValue",o->MItem.TrapValue,false,false);                      // 22
			DRAW_COMPONENT("Value0",o->MItem.Val[0],false,false);                            // 23
			DRAW_COMPONENT("Value1",o->MItem.Val[1],false,false);                            // 24
			DRAW_COMPONENT("Value2",o->MItem.Val[2],false,false);                            // 25
			DRAW_COMPONENT("Value3",o->MItem.Val[3],false,false);                            // 26
			DRAW_COMPONENT("Value4",o->MItem.Val[4],false,false);                            // 27

			switch(proto->GetType())
			{
			case ITEM_TYPE_ARMOR:
				DRAW_COMPONENT("DeteorationFlags",o->MItem.DeteorationFlags,true,false);     // 28
				DRAW_COMPONENT("DeteorationCount",o->MItem.DeteorationCount,true,false);     // 29
				DRAW_COMPONENT("DeteorationValue",o->MItem.DeteorationValue,true,false);     // 30
				break;
			case ITEM_TYPE_WEAPON:
				if(proto->WeapIsGrouped())
				{
					DRAW_COMPONENT("Count",o->MItem.Count,true,false);                       // 28
				}
				else if(proto->WeapIsWeared())
				{
					DRAW_COMPONENT("DeteorationFlags",o->MItem.DeteorationFlags,true,false); // 28
					DRAW_COMPONENT("DeteorationCount",o->MItem.DeteorationCount,true,false); // 29
					DRAW_COMPONENT("DeteorationValue",o->MItem.DeteorationValue,true,false); // 30
					if(proto->Weapon.VolHolder)
					{
						DRAW_COMPONENT("AmmoPid",o->MItem.AmmoPid,true,false);               // 31
						DRAW_COMPONENT("AmmoCount",o->MItem.AmmoCount,true,false);           // 32
					}
				}
				break;
			case ITEM_TYPE_DRUG:
			case ITEM_TYPE_AMMO:
			case ITEM_TYPE_MISC:
				DRAW_COMPONENT("Count",o->MItem.Count,true,false);                           // 28
				break;
			case ITEM_TYPE_KEY:
				DRAW_COMPONENT("LockerDoorId",o->MItem.LockerDoorId,true,false);             // 28
				break;
			case ITEM_TYPE_CONTAINER:
			case ITEM_TYPE_DOOR:
				DRAW_COMPONENT("LockerDoorId",o->MItem.LockerDoorId,true,false);             // 28
				DRAW_COMPONENT("LockerCondition",o->MItem.LockerCondition,true,false);       // 29
				DRAW_COMPONENT("LockerComplexity",o->MItem.LockerComplexity,true,false);     // 30
				break;
			default:
				break;
			}
		}
		else if(o->MapObjType==MAP_OBJECT_SCENERY)
		{
			if(proto->GetType()==ITEM_TYPE_GRID)
			{
				DRAW_COMPONENT("ToMapPid",o->MScenery.ToMapPid,true,false);                  // 22
				if(o->ProtoId==SP_GRID_ENTIRE)
					DRAW_COMPONENT("EntireNumber",o->MScenery.ToEntire,true,false);          // 23
				else
					DRAW_COMPONENT("ToEntire",o->MScenery.ToEntire,true,false);              // 23
				DRAW_COMPONENT("ToMapX",o->MScenery.ToMapX,true,false);                      // 24
				DRAW_COMPONENT("ToMapY",o->MScenery.ToMapY,true,false);                      // 25
				DRAW_COMPONENT("ToDir",o->MScenery.ToDir,true,false);                        // 26
			}
			else if(proto->GetType()==ITEM_TYPE_GENERIC)
			{
				DRAW_COMPONENT("ParamsCount",o->MScenery.ParamsCount,true,false);            // 22
				DRAW_COMPONENT("Parameter0",o->MScenery.Param[0],false,false);               // 23
				DRAW_COMPONENT("Parameter1",o->MScenery.Param[1],false,false);               // 24
				DRAW_COMPONENT("Parameter2",o->MScenery.Param[2],false,false);               // 25
				DRAW_COMPONENT("Parameter3",o->MScenery.Param[3],false,false);               // 26
				DRAW_COMPONENT("Parameter4",o->MScenery.Param[4],false,false);               // 27
				if(o->ProtoId==SP_SCEN_TRIGGER)
				{
					DRAW_COMPONENT("TriggerNum",o->MScenery.TriggerNum,true,false);          // 28
				}
				else
				{
					DRAW_COMPONENT("CanUse",o->MScenery.CanUse,true,false);                  // 28
					DRAW_COMPONENT("CanTalk",o->MScenery.CanTalk,true,false);                // 29
				}
			}
		}
	}
}

void FOMapper::ObjKeyDown(BYTE dik)
{
	if(!ObjVisible) return;
	if(ConsoleEdit) return;
	if(SelectedObj.empty()) return;

	HexMngr.RebuildLight();

	if(IntMode==INT_MODE_INCONT && InContObject)
	{
		ObjKeyDownA(InContObject,dik);
		return;
	}

	MapObject* o=SelectedObj[0].MapObj;
	ProtoItem* proto=ItemMngr.GetProtoItem(o->ProtoId);
	for(int i=0,j=ObjToAll?SelectedObj.size():1;i<j;i++) // At least one time
	{
		MapObject* o2=SelectedObj[i].MapObj;
		ProtoItem* proto2=ItemMngr.GetProtoItem(o2->ProtoId);

		if(o->MapObjType==o2->MapObjType && (o->MapObjType==MAP_OBJECT_CRITTER || (proto && proto2 && proto->GetType()==proto2->GetType())))
		{
			ObjKeyDownA(o2,dik);

			if(SelectedObj[i].IsItem()) HexMngr.AffectItem(o2,SelectedObj[i].MapItem);
			else if(SelectedObj[i].IsNpc()) HexMngr.AffectCritter(o2,SelectedObj[i].MapNpc);
		}
	}
}

void FOMapper::ObjKeyDownA(MapObject* o, BYTE dik)
{
	char* val_c=NULL;
	BYTE* val_b=NULL;
	short* val_s=NULL;
	WORD* val_w=NULL;
	DWORD* val_dw=NULL;
	int* val_i=NULL;
	bool* val_bool=NULL;

	ProtoItem* proto=(o->MapObjType!=MAP_OBJECT_CRITTER?ItemMngr.GetProtoItem(o->ProtoId):NULL);
	if(o->MapObjType!=MAP_OBJECT_CRITTER && !proto) return;

	if(o->MapObjType==MAP_OBJECT_CRITTER && ObjCurLine>=16 && ObjCurLine<=16+MAPOBJ_CRITTER_PARAMS-1)
	{
		int pos=ObjCurLine-16;
		for(int i=0;i<MAPOBJ_CRITTER_PARAMS;i++)
		{
			if(o->MCritter.ParamIndex[i]>=0 && o->MCritter.ParamIndex[i]<MAX_PARAMS)
			{
				if(!pos)
				{
					val_i=&o->MCritter.ParamValue[i];
					break;
				}
				pos--;
			}
		}
	}

	switch(ObjCurLine)
	{
	case 6: if(o->MapObjType!=MAP_OBJECT_CRITTER) val_s=&o->Dir; break;
	case 7: Keyb::GetChar(dik,o->ScriptName,NULL,MAPOBJ_SCRIPT_NAME,KIF_NO_SPEC_SYMBOLS); return;
	case 8: Keyb::GetChar(dik,o->FuncName,NULL,MAPOBJ_SCRIPT_NAME,KIF_NO_SPEC_SYMBOLS); return;
	case 9: val_c=&o->LightIntensity; break;
	case 10: val_b=&o->LightDistance; break;
	case 11: val_dw=&o->LightColor; break;
	case 12: val_b=&o->LightDirOff; break;
	case 13: val_b=&o->LightDay; break;

	case 14:
		if(o->MapObjType==MAP_OBJECT_CRITTER) val_b=&o->MCritter.Cond;
		else val_s=&o->MItem.OffsetX;
		break;
	case 15:
		if(o->MapObjType==MAP_OBJECT_CRITTER) val_b=&o->MCritter.CondExt;
		else val_s=&o->MItem.OffsetY;
		break;
	case 16:
		if(o->MapObjType==MAP_OBJECT_CRITTER) break;
		else val_b=&o->MItem.AnimStayBegin;
		break;
	case 17:
		if(o->MapObjType==MAP_OBJECT_CRITTER) break;
		else val_b=&o->MItem.AnimStayEnd;
		break;
	case 18:
		if(o->MapObjType==MAP_OBJECT_CRITTER) break;
		else val_w=&o->MItem.AnimWait;
		break;
	case 19:
		if(o->MapObjType==MAP_OBJECT_CRITTER) break;
		else
		{
			Keyb::GetChar(dik,o->RunTime.PicMapName,NULL,sizeof(o->RunTime.PicMapName),KIF_NO_SPEC_SYMBOLS);
			return;
		}
		break;
	case 20:
		if(o->MapObjType==MAP_OBJECT_CRITTER) break;
		else
		{
			Keyb::GetChar(dik,o->RunTime.PicInvName,NULL,sizeof(o->RunTime.PicInvName),KIF_NO_SPEC_SYMBOLS);
			return;
		}
		break;
	case 21:
		if(o->MapObjType==MAP_OBJECT_CRITTER) break;
		else val_b=&o->MItem.InfoOffset;
		break;

	case 22:
		if(o->MapObjType==MAP_OBJECT_ITEM) val_s=&o->MItem.TrapValue;
		else if(o->MapObjType==MAP_OBJECT_SCENERY)
		{
			if(proto->GetType()==ITEM_TYPE_GRID) val_w=&o->MScenery.ToMapPid;
			else if(proto->GetType()==ITEM_TYPE_GENERIC) val_b=&o->MScenery.ParamsCount;
		}
		break;
	case 23:
		if(o->MapObjType==MAP_OBJECT_ITEM) val_i=&o->MItem.Val[0];
		else if(o->MapObjType==MAP_OBJECT_SCENERY)
		{
			if(proto->GetType()==ITEM_TYPE_GRID) val_dw=&o->MScenery.ToEntire;
			else if(proto->GetType()==ITEM_TYPE_GENERIC) val_i=&o->MScenery.Param[0];
		}
		break;
	case 24:
		if(o->MapObjType==MAP_OBJECT_ITEM) val_i=&o->MItem.Val[1];
		else if(o->MapObjType==MAP_OBJECT_SCENERY)
		{
			if(proto->GetType()==ITEM_TYPE_GRID) val_w=&o->MScenery.ToMapX;
			else if(proto->GetType()==ITEM_TYPE_GENERIC) val_i=&o->MScenery.Param[1];
		}
		break;
	case 25:
		if(o->MapObjType==MAP_OBJECT_ITEM) val_i=&o->MItem.Val[2];
		else if(o->MapObjType==MAP_OBJECT_SCENERY)
		{
			if(proto->GetType()==ITEM_TYPE_GRID) val_w=&o->MScenery.ToMapY;
			else if(proto->GetType()==ITEM_TYPE_GENERIC) val_i=&o->MScenery.Param[2];
		}
		break;
	case 26:
		if(o->MapObjType==MAP_OBJECT_ITEM) val_i=&o->MItem.Val[3];
		else if(o->MapObjType==MAP_OBJECT_SCENERY)
		{
			if(proto->GetType()==ITEM_TYPE_GRID) val_b=&o->MScenery.ToDir;
			else if(proto->GetType()==ITEM_TYPE_GENERIC) val_i=&o->MScenery.Param[3];
		}
		break;
	case 27:
		if(o->MapObjType==MAP_OBJECT_ITEM) val_i=&o->MItem.Val[4];
		else if(o->MapObjType==MAP_OBJECT_SCENERY && proto->GetType()==ITEM_TYPE_GENERIC) val_i=&o->MScenery.Param[4];
		break;
	case 28:
		if(o->MapObjType==MAP_OBJECT_ITEM)
		{
			switch(proto->GetType())
			{
			case ITEM_TYPE_WEAPON:
				if(proto->WeapIsGrouped()) val_dw=&o->MItem.Count;
				else if(proto->WeapIsWeared()) val_b=&o->MItem.DeteorationFlags;
				break;
			case ITEM_TYPE_ARMOR: val_b=&o->MItem.DeteorationFlags; break;
			case ITEM_TYPE_KEY:
			case ITEM_TYPE_CONTAINER:
			case ITEM_TYPE_DOOR: val_dw=&o->MItem.LockerDoorId; break;
			case ITEM_TYPE_DRUG:
			case ITEM_TYPE_AMMO:
			case ITEM_TYPE_MISC: val_dw=&o->MItem.Count; break;
			default: break;
			}
		}
		else if(o->MapObjType==MAP_OBJECT_SCENERY && proto->GetType()==ITEM_TYPE_GENERIC)
		{
			if(o->ProtoId==SP_SCEN_TRIGGER) val_dw=&o->MScenery.TriggerNum;
			else val_bool=&o->MScenery.CanUse;
		}
		break;
	case 29:
		if(o->MapObjType==MAP_OBJECT_ITEM)
		{
			switch(proto->GetType())
			{
			case ITEM_TYPE_WEAPON: if(proto->WeapIsWeared()) val_b=&o->MItem.DeteorationCount; break;
			case ITEM_TYPE_ARMOR: val_b=&o->MItem.DeteorationCount; break;
			case ITEM_TYPE_CONTAINER:
			case ITEM_TYPE_DOOR: val_w=&o->MItem.LockerCondition; break;
			default: break;
			}
		}
		else if(o->MapObjType==MAP_OBJECT_SCENERY && proto->GetType()==ITEM_TYPE_GENERIC && o->ProtoId!=SP_SCEN_TRIGGER) val_bool=&o->MScenery.CanTalk;
		break;
	case 30:
		if(o->MapObjType==MAP_OBJECT_ITEM)
		{
			switch(proto->GetType())
			{
			case ITEM_TYPE_WEAPON: if(proto->WeapIsWeared()) val_w=&o->MItem.DeteorationValue; break;
			case ITEM_TYPE_ARMOR: val_w=&o->MItem.DeteorationValue; break;
			case ITEM_TYPE_CONTAINER:
			case ITEM_TYPE_DOOR: val_w=&o->MItem.LockerComplexity; break;
			default: break;
			}
		}
		break;
	case 31:
		if(o->MapObjType==MAP_OBJECT_ITEM && proto->GetType()==ITEM_TYPE_WEAPON && proto->WeapIsWeared() && proto->Weapon.VolHolder) val_w=&o->MItem.AmmoPid;
		break;
	case 32:
		if(o->MapObjType==MAP_OBJECT_ITEM && proto->GetType()==ITEM_TYPE_WEAPON && proto->WeapIsWeared() && proto->Weapon.VolHolder) val_dw=&o->MItem.AmmoCount;
		break;
	default:
		break;
	}

	int add=0;

	switch(dik)
	{
	case DIK_0:
	case DIK_NUMPAD0: add=0; break;
	case DIK_1:
	case DIK_NUMPAD1: add=1; break;
	case DIK_2:
	case DIK_NUMPAD2: add=2; break;
	case DIK_3:
	case DIK_NUMPAD3: add=3; break;
	case DIK_4:
	case DIK_NUMPAD4: add=4; break;
	case DIK_5:
	case DIK_NUMPAD5: add=5; break;
	case DIK_6:
	case DIK_NUMPAD6: add=6; break;
	case DIK_7:
	case DIK_NUMPAD7: add=7; break;
	case DIK_8:
	case DIK_NUMPAD8: add=8; break;
	case DIK_9:
	case DIK_NUMPAD9: add=9; break;
//	case DIK_DELETE:
	case DIK_BACKSPACE:
		if(val_c) *val_c=*val_c/10;
		if(val_b) *val_b=*val_b/10;
		if(val_s) *val_s=*val_s/10;
		if(val_w) *val_w=*val_w/10;
		if(val_dw) *val_dw=*val_dw/10;
		if(val_i) *val_i=*val_i/10;
		if(val_bool) *val_bool=false;
		return;
	case DIK_MINUS:
	case DIK_SUBTRACT:
		if(val_c) *val_c=-*val_c;
		if(val_s) *val_s=-*val_s;
		if(val_i) *val_i=-*val_i;
		if(val_bool) *val_bool=!*val_bool;
		return;
	default:
		return;
	}

	if(val_b==&o->MCritter.Cond)
	{
		*val_b=add;
		return;
	}

	if(val_c) *val_c=*val_c*10+add;
	if(val_b) *val_b=*val_b*10+add;
	if(val_s) *val_s=*val_s*10+add;
	if(val_w) *val_w=*val_w*10+add;
	if(val_dw) *val_dw=*val_dw*10+add;
	if(val_i) *val_i=*val_i*10+add;
	if(val_bool) *val_bool=(add?true:false);
}

void FOMapper::IntLMouseDown()
{
	IntHold=INT_NONE;

/************************************************************************/
/* MAP                                                                  */
/************************************************************************/
	if((!IntVisible || !IsCurInRect(IntWMain,IntX,IntY)) && (!ObjVisible || SelectedObj.empty() || !IsCurInRect(ObjWMain,ObjX,ObjY)))
	{
		InContObject=NULL;

		if(!HexMngr.GetHexPixel(CurX,CurY,SelectHX1,SelectHY1)) return;
		SelectHX2=SelectHX1;
		SelectHY2=SelectHY1;

		if(Keyb::ShiftDwn)
		{
			for(int i=0,j=SelectedObj.size();i<j;i++)
			{
				SelMapObj& so=SelectedObj[i];
				if(so.MapNpc)
				{
					BYTE crtype=so.MapNpc->GetCrType();
					bool is_run=(so.MapNpc->MoveSteps.size() && so.MapNpc->MoveSteps[so.MapNpc->MoveSteps.size()-1].first==SelectHX1 && so.MapNpc->MoveSteps[so.MapNpc->MoveSteps.size()-1].second==SelectHY1/* && CritType::IsCanRun(crtype)*/);
					so.MapNpc->MoveSteps.clear();
					if(!is_run && !CritType::IsCanWalk(crtype)) break;
					WORD hx=so.MapNpc->GetHexX();
					WORD hy=so.MapNpc->GetHexY();
					ByteVec steps;
					if(HexMngr.FindStep(hx,hy,SelectHX1,SelectHY1,steps)==FP_OK)
					{
						for(int k=0;k<steps.size();k++)
						{
							MoveHexByDir(hx,hy,steps[k],HexMngr.GetMaxHexX(),HexMngr.GetMaxHexY());
							so.MapNpc->MoveSteps.push_back(WordPair(hx,hy));
						}
						so.MapNpc->IsRunning=is_run;
					}
					break;
				}
			}
			return;
		}

		if(CurMode==CUR_MODE_DEF)
		{
			if(!Keyb::CtrlDwn) SelectClear();
			//HexMngr.ClearHexTrack();
			//HexMngr.RefreshMap();
			IntHold=INT_SELECT;
		}
		else if(CurMode==CUR_MODE_MOVE)
		{
			IntHold=INT_SELECT;
		}
		else if(CurMode==CUR_MODE_DRAW)
		{
			if(IsObjectMode() && (*CurItemProtos).size()) ParseProto((*CurItemProtos)[CurProto[IntMode]].GetPid(),SelectHX1,SelectHY1,false);
			else if(IsTileMode() && TilesPictures.size()) ParseTile(TilesPictures[CurProto[IntMode]],SelectHX1,SelectHY1);
			else if(IsCritMode() && NpcProtos.size()) ParseNpc(NpcProtos[CurProto[IntMode]]->ProtoId,SelectHX1,SelectHY1);
		}

		return;
	}
/************************************************************************/
/* OBJECT EDITOR                                                        */
/************************************************************************/
	if(ObjVisible && !SelectedObj.empty() && IsCurInRect(ObjWMain,ObjX,ObjY))
	{
		if(IsCurInRect(ObjWWork,ObjX,ObjY))
		{
			ObjCurLine=(CurY-ObjY-ObjWWork[1])/DRAW_NEXT_HEIGHT;
	//		return;
		}

		if(IsCurInRect(ObjBToAll,ObjX,ObjY))
		{
			ObjToAll=!ObjToAll;
			IntHold=INT_BUTTON;
			return;
		}
		else if(!ObjFix)
		{
			IntHold=INT_OBJECT;
			ItemVectX=CurX-ObjX;
			ItemVectY=CurY-ObjY;
		}

		return;
	}
/************************************************************************/
/* INTERFACE                                                            */
/************************************************************************/
	if(!IntVisible || !IsCurInRect(IntWMain,IntX,IntY)) return;

	if(IsCurInRect(IntWWork,IntX,IntY))
	{
		int ind=(CurX-IntX-IntWWork[0])/ProtoWidth;

		if(IsObjectMode() && (*CurItemProtos).size())
		{
			ind+=*CurProtoScroll;
			if(ind>=(*CurItemProtos).size()) ind=(*CurItemProtos).size()-1;
			CurProto[IntMode]=ind;

			// Switch ignore pid to draw
			if(Keyb::CtrlDwn)
			{
				WORD pid=(*CurItemProtos)[ind].GetPid();

				ProtoItemVecIt it=std::find(IgnoreItemProto.begin(),IgnoreItemProto.end(),pid);
				if(it!=IgnoreItemProto.end()) IgnoreItemProto.erase(it);
				else IgnoreItemProto.push_back((*CurItemProtos)[ind]);

				HexMngr.SwitchIgnorePid(pid);
				HexMngr.RefreshMap();
			}
			// Add to container
			else if(Keyb::AltDwn && SelectedObj.size() && SelectedObj[0].IsContainer())
			{
				bool add=true;
				ProtoItem* proto_item=&(*CurItemProtos)[ind];

				if(proto_item->IsGrouped())
				{
					for(int i=0,j=SelectedObj[0].Childs.size();i<j;i++)
					{
						if(proto_item->GetPid()==SelectedObj[0].Childs[i]->ProtoId)
						{
							add=false;
							break;
						}
					}
				}

				if(add)
				{
					MapObject* mobj=SelectedObj[0].MapObj;
					ParseProto(proto_item->GetPid(),mobj->MapX,mobj->MapY,true);
					SelectAdd(mobj);
				}
			}
		}
		else if(IsTileMode() && TilesPictures.size())
		{
			ind+=*CurProtoScroll;
			if(ind>=TilesPictures.size()) ind=TilesPictures.size()-1;
			CurProto[IntMode]=ind;
		}
		else if(IsCritMode() && NpcProtos.size())
		{
			ind+=*CurProtoScroll;
			if(ind>=NpcProtos.size()) ind=NpcProtos.size()-1;
			CurProto[IntMode]=ind;
		}
		else if(IntMode==INT_MODE_INCONT)
		{
			InContObject=NULL;
			ind+=InContScroll;	

			if(!SelectedObj.empty() && !SelectedObj[0].Childs.empty())
			{
				if(ind<SelectedObj[0].Childs.size()) InContObject=SelectedObj[0].Childs[ind];

				// Delete child
				if(Keyb::AltDwn && InContObject)
				{
					MapObjectPtrVecIt it=std::find(CurProtoMap->MObjects.begin(),CurProtoMap->MObjects.end(),InContObject);
					if(it!=CurProtoMap->MObjects.end())
					{
						SAFEREL(InContObject);
						CurProtoMap->MObjects.erase(it);
					}
					InContObject=NULL;

					// Reselect
					MapObject* mobj=SelectedObj[0].MapObj;
					SelectClear();
					SelectAdd(mobj);
				}
				// Change child slot
				else if(Keyb::ShiftDwn && InContObject && SelectedObj[0].MapNpc)
				{
					ProtoItem* proto_item=ItemMngr.GetProtoItem(InContObject->ProtoId);
					BYTE crtype=SelectedObj[0].MapNpc->GetCrType();
					BYTE anim1=(proto_item->IsWeapon()?proto_item->Weapon.Anim1:0);

					if(proto_item->IsArmor() && CritType::IsCanArmor(crtype))
					{
						if(InContObject->MItem.ItemSlot==SLOT_ARMOR)
						{
							InContObject->MItem.ItemSlot=SLOT_INV;
						}
						else
						{
							for(int i=0;i<SelectedObj[0].Childs.size();i++)
							{
								MapObject* child=SelectedObj[0].Childs[i];
								if(child->MItem.ItemSlot==SLOT_ARMOR) child->MItem.ItemSlot=SLOT_INV;
							}
							InContObject->MItem.ItemSlot=SLOT_ARMOR;
						}
					}
					else if(!proto_item->IsArmor() && (!anim1 || CritType::IsAnim1(crtype,anim1)))
					{
						if(InContObject->MItem.ItemSlot==SLOT_HAND1)
						{
							for(int i=0;i<SelectedObj[0].Childs.size();i++)
							{
								MapObject* child=SelectedObj[0].Childs[i];
								if(child->MItem.ItemSlot==SLOT_HAND2) child->MItem.ItemSlot=SLOT_INV;
							}
							InContObject->MItem.ItemSlot=SLOT_HAND2;
						}
						else if(InContObject->MItem.ItemSlot==SLOT_HAND2)
						{
							InContObject->MItem.ItemSlot=SLOT_INV;
						}
						else
						{
							for(int i=0;i<SelectedObj[0].Childs.size();i++)
							{
								MapObject* child=SelectedObj[0].Childs[i];
								if(child->MItem.ItemSlot==SLOT_HAND1) child->MItem.ItemSlot=SLOT_INV;
							}
							InContObject->MItem.ItemSlot=SLOT_HAND1;
						}
					}
				}

				if(SelectedObj[0].MapNpc)
				{
					ProtoItem* pitem_ext=NULL;
					ProtoItem* pitem_armor=NULL;
					for(int i=0;i<SelectedObj[0].Childs.size();i++)
					{
						MapObject* child=SelectedObj[0].Childs[i];
						if(child->MItem.ItemSlot==SLOT_HAND2) pitem_ext=ItemMngr.GetProtoItem(child->ProtoId);
						else if(child->MItem.ItemSlot==SLOT_ARMOR) pitem_armor=ItemMngr.GetProtoItem(child->ProtoId);
					}
					SelectedObj[0].MapNpc->DefItemSlotExt.Init(pitem_ext?pitem_ext:ItemMngr.GetProtoItem(ITEM_DEF_SLOT));
					SelectedObj[0].MapNpc->DefItemSlotArmor.Init(pitem_armor?pitem_armor:ItemMngr.GetProtoItem(ITEM_DEF_ARMOR));

					ProtoItem* pitem_main=NULL;
					for(int i=0;i<SelectedObj[0].Childs.size();i++)
					{
						MapObject* child=SelectedObj[0].Childs[i];
						if(child->MItem.ItemSlot==SLOT_HAND1)
						{
							pitem_main=ItemMngr.GetProtoItem(child->ProtoId);
							BYTE anim1=(pitem_main->IsWeapon()?pitem_main->Weapon.Anim1:0);
							if(anim1 && !CritType::IsAnim1(SelectedObj[0].MapNpc->GetCrType(),anim1))
							{
								pitem_main=NULL;
								child->MItem.ItemSlot=SLOT_INV;
							}
						}
					}
					SelectedObj[0].MapNpc->DefItemSlotMain.Init(pitem_main?pitem_main:ItemMngr.GetProtoItem(ITEM_DEF_SLOT));
					SelectedObj[0].MapNpc->AnimateStay();
				}
				HexMngr.RebuildLight();
			}
		}
		else if(IntMode==INT_MODE_LIST)
		{
			ind+=ListScroll;

			if(ind<LoadedProtoMaps.size() && CurProtoMap!=LoadedProtoMaps[ind])
			{
				HexMngr.GetScreenHexes(CurProtoMap->Header.CenterX,CurProtoMap->Header.CenterY);
				SelectClear();

				if(HexMngr.SetProtoMap(*LoadedProtoMaps[ind]))
				{
					CurProtoMap=LoadedProtoMaps[ind];
					HexMngr.FindSetCenter(CurProtoMap->Header.CenterX,CurProtoMap->Header.CenterY);
				}
			}
		}
	}
	else if(IsCurInRect(IntBArm,IntX,IntY)) IntSetMode(INT_MODE_ARMOR);
	else if(IsCurInRect(IntBDrug,IntX,IntY)) IntSetMode(INT_MODE_DRUG);
	else if(IsCurInRect(IntBWpn,IntX,IntY)) IntSetMode(INT_MODE_WEAPON);
	else if(IsCurInRect(IntBAmmo,IntX,IntY)) IntSetMode(INT_MODE_AMMO);
	else if(IsCurInRect(IntBMisc,IntX,IntY)) IntSetMode(INT_MODE_MISC);
	else if(IsCurInRect(IntBMisc2,IntX,IntY)) IntSetMode(INT_MODE_MISC2);
	else if(IsCurInRect(IntBKey,IntX,IntY)) IntSetMode(INT_MODE_KEY);
	else if(IsCurInRect(IntBCont,IntX,IntY)) IntSetMode(INT_MODE_CONT);
	else if(IsCurInRect(IntBDoor,IntX,IntY)) IntSetMode(INT_MODE_DOOR);
	else if(IsCurInRect(IntBGrid,IntX,IntY)) IntSetMode(INT_MODE_GRID);
	else if(IsCurInRect(IntBGen,IntX,IntY)) IntSetMode(INT_MODE_GENERIC);
	else if(IsCurInRect(IntBWall,IntX,IntY)) IntSetMode(INT_MODE_WALL);
	else if(IsCurInRect(IntBTile,IntX,IntY)) IntSetMode(INT_MODE_TILE);
	else if(IsCurInRect(IntBCrit,IntX,IntY)) IntSetMode(INT_MODE_CRIT);
	else if(IsCurInRect(IntBFast,IntX,IntY)) IntSetMode(INT_MODE_FAST);
	else if(IsCurInRect(IntBIgnore,IntX,IntY)) IntSetMode(INT_MODE_IGNORE);
	else if(IsCurInRect(IntBInCont,IntX,IntY)) IntSetMode(INT_MODE_INCONT);
	else if(IsCurInRect(IntBMess,IntX,IntY)) IntSetMode(INT_MODE_MESS);
	else if(IsCurInRect(IntBList,IntX,IntY)) IntSetMode(INT_MODE_LIST);
	else if(IsCurInRect(IntBScrBack,IntX,IntY))
	{
		if(IsObjectMode() || IsTileMode() || IsCritMode())
		{
			(*CurProtoScroll)--;
			if(*CurProtoScroll<0) *CurProtoScroll=0;
		}
		else if(IntMode==INT_MODE_INCONT)
		{
			InContScroll--;
			if(InContScroll<0) InContScroll=0;
		}
		else if(IntMode==INT_MODE_LIST)
		{
			ListScroll--;
			if(ListScroll<0) ListScroll=0;
		}
	}
	else if(IsCurInRect(IntBScrBackFst,IntX,IntY))
	{
		if(IsObjectMode() || IsTileMode() || IsCritMode())
		{
			(*CurProtoScroll)-=ProtosOnScreen;
			if(*CurProtoScroll<0) *CurProtoScroll=0;
		}
		else if(IntMode==INT_MODE_INCONT)
		{
			InContScroll-=ProtosOnScreen;
			if(InContScroll<0) InContScroll=0;
		}
		else if(IntMode==INT_MODE_LIST)
		{
			ListScroll-=ProtosOnScreen;
			if(ListScroll<0) ListScroll=0;
		}
	}
	else if(IsCurInRect(IntBScrFront,IntX,IntY))
	{
		if(IsObjectMode() && (*CurItemProtos).size())
		{
			(*CurProtoScroll)++;
			if(*CurProtoScroll>=(*CurItemProtos).size()) *CurProtoScroll=(*CurItemProtos).size()-1;
		}
		else if(IsTileMode() && TilesPictures.size())
		{
			(*CurProtoScroll)++;
			if(*CurProtoScroll>=TilesPictures.size()) *CurProtoScroll=TilesPictures.size()-1;
		}
		else if(IsCritMode() && NpcProtos.size())
		{
			(*CurProtoScroll)++;
			if(*CurProtoScroll>=NpcProtos.size()) *CurProtoScroll=NpcProtos.size()-1;
		}
		else if(IntMode==INT_MODE_INCONT) InContScroll++;
		else if(IntMode==INT_MODE_LIST) ListScroll++;
	}
	else if(IsCurInRect(IntBScrFrontFst,IntX,IntY))
	{
		if(IsObjectMode() && (*CurItemProtos).size())
		{
			(*CurProtoScroll)+=ProtosOnScreen;
			if(*CurProtoScroll>=(*CurItemProtos).size()) *CurProtoScroll=(*CurItemProtos).size()-1;
		}
		else if(IsTileMode() && TilesPictures.size())
		{
			(*CurProtoScroll)+=ProtosOnScreen;
			if(*CurProtoScroll>=TilesPictures.size()) *CurProtoScroll=TilesPictures.size()-1;
		}
		else if(IsCritMode() && NpcProtos.size())
		{
			(*CurProtoScroll)+=ProtosOnScreen;
			if(*CurProtoScroll>=NpcProtos.size()) *CurProtoScroll=NpcProtos.size()-1;
		}
		else if(IntMode==INT_MODE_INCONT) InContScroll+=ProtosOnScreen;
		else if(IntMode==INT_MODE_LIST) ListScroll+=ProtosOnScreen;
	}
	else if(IsCurInRect(IntBShowItem,IntX,IntY)) { OptShowItem=!OptShowItem; HexMngr.RefreshMap(); }
	else if(IsCurInRect(IntBShowScen,IntX,IntY)) { OptShowScen=!OptShowScen; HexMngr.RefreshMap(); }
	else if(IsCurInRect(IntBShowWall,IntX,IntY)) { OptShowWall=!OptShowWall; HexMngr.RefreshMap(); }
	else if(IsCurInRect(IntBShowCrit,IntX,IntY)) { OptShowCrit=!OptShowCrit; HexMngr.RefreshMap(); }
	else if(IsCurInRect(IntBShowTile,IntX,IntY)) { OptShowTile=!OptShowTile; HexMngr.RefreshMap(); }
	else if(IsCurInRect(IntBShowRoof,IntX,IntY)) { OptShowRoof=!OptShowRoof; HexMngr.RefreshMap(); }
	else if(IsCurInRect(IntBShowFast,IntX,IntY)) { OptShowFast=!OptShowFast; HexMngr.RefreshMap(); }
	else if(IsCurInRect(IntBSelectItem,IntX,IntY)) IsSelectItem=!IsSelectItem;
	else if(IsCurInRect(IntBSelectScen,IntX,IntY)) IsSelectScen=!IsSelectScen;
	else if(IsCurInRect(IntBSelectWall,IntX,IntY)) IsSelectWall=!IsSelectWall;
	else if(IsCurInRect(IntBSelectCrit,IntX,IntY)) IsSelectCrit=!IsSelectCrit;
	else if(IsCurInRect(IntBSelectTile,IntX,IntY)) IsSelectTile=!IsSelectTile;
	else if(IsCurInRect(IntBSelectRoof,IntX,IntY)) IsSelectRoof=!IsSelectRoof;
	else if(!IntFix)
	{
		IntHold=INT_MAIN;
		IntVectX=CurX-IntX;
		IntVectY=CurY-IntY;
		return;
	}
	else return;

	IntHold=INT_BUTTON;
/************************************************************************/
/*                                                                      */
/************************************************************************/
}

void FOMapper::IntLMouseUp()
{
	if(IntHold==INT_SELECT)
	{
		if(!HexMngr.GetHexPixel(CurX,CurY,SelectHX2,SelectHY2)) return;

		if(CurMode==CUR_MODE_DEF)
		{
			if(SelectHX1!=SelectHX2 || SelectHY1!=SelectHY2)
			{
				HexMngr.ClearHexTrack();
				WordPairVec h;

				if(SelectType==SELECT_TYPE_OLD)
				{
					int fx=min(SelectHX1,SelectHX2);
					int tx=max(SelectHX1,SelectHX2);
					int fy=min(SelectHY1,SelectHY2);
					int ty=max(SelectHY1,SelectHY2);

					for(int i=fx;i<=tx;i++)
					{
						for(int j=fy;j<=ty;j++)
						{
							h.push_back(WordPairVecVal(i,j));
						}
					}
				}
				else if(SelectType==SELECT_TYPE_NEW)
				{
					HexMngr.GetHexesRect(INTRECT(SelectHX1,SelectHY1,SelectHX2,SelectHY2),h);
				}
				else
					return;

				ItemHexVec items;
				CritVec critters;
				for(int i=0,j=h.size();i<j;i++)
				{
					WORD hx=h[i].first;
					WORD hy=h[i].second;

					// Items, critters
					HexMngr.GetItems(hx,hy,items);
					HexMngr.GetCritters(hx,hy,critters,FIND_ALL);

					// Tile, roof
					if(!(hx%2) && !(hy%2))
					{
						DWORD tile=CurProtoMap->GetTile(hx/2,hy/2);
						DWORD roof=CurProtoMap->GetRoof(hx/2,hy/2);

						if(IsSelectTile && OptShowTile /*&& !HexMngr.IsIgnorePid(tile)*/) SelectAddTile(hx/2,hy/2,tile,false,true);
						if(IsSelectRoof && OptShowRoof /*&& !HexMngr.IsIgnorePid(roof)*/) SelectAddTile(hx/2,hy/2,roof,true,true);
					}
				}

				for(int k=0;k<items.size();k++)
				{
					WORD pid=items[k]->GetProtoId();
					if(HexMngr.IsIgnorePid(pid)) continue;
					if(!OptShowFast && HexMngr.IsFastPid(pid)) continue;

					if(items[k]->IsItem() && IsSelectItem && OptShowItem) SelectAddItem(items[k]);
					else if(items[k]->IsScenOrGrid() && IsSelectScen && OptShowScen) SelectAddItem(items[k]);
					else if(items[k]->IsWall() && IsSelectWall && OptShowWall) SelectAddItem(items[k]);
					else if(OptShowFast && HexMngr.IsFastPid(pid)) SelectAddItem(items[k]);
				}

				for(int l=0;l<critters.size();l++)
				{
					if(IsSelectCrit && OptShowCrit) SelectAddCrit(critters[l]);
				}
			}
			else
			{
				ItemHex* item;
				CritterCl* cr;
				HexMngr.GetSmthPixel(CurX,CurY,item,cr);

				if(item)
				{
					if(!HexMngr.IsIgnorePid(item->GetProtoId())) SelectAddItem(item);
				}
				else if(cr)
				{
					SelectAddCrit(cr);
				}
			}

		//	if(SelectedObj.size() || SelectedTile.size()) CurMode=CUR_MODE_MOVE;

			// Crits or item container
			if(SelectedObj.size() && SelectedObj[0].IsContainer()) IntSetMode(INT_MODE_INCONT);
		}

		HexMngr.RefreshMap();
	}

	IntHold=INT_NONE;
}

void FOMapper::IntMouseMove()
{
	if(IntHold==INT_SELECT)
	{
		HexMngr.ClearHexTrack();
		if(!HexMngr.GetHexPixel(CurX,CurY,SelectHX2,SelectHY2)) return;

		if(SelectHX1!=SelectHX2 || SelectHY1!=SelectHY2)
		{
			if(CurMode==CUR_MODE_DEF)
			{
				if(SelectType==SELECT_TYPE_OLD)
				{
					int fx=min(SelectHX1,SelectHX2);
					int tx=max(SelectHX1,SelectHX2);
					int fy=min(SelectHY1,SelectHY2);
					int ty=max(SelectHY1,SelectHY2);

					for(int i=fx;i<=tx;i++)
					{
						for(int j=fy;j<=ty;j++)
						{
							HexMngr.GetHexTrack(i,j)=1;
						}
					}
				}
				else if(SelectType==SELECT_TYPE_NEW)
				{
					WordPairVec h;
					HexMngr.GetHexesRect(INTRECT(SelectHX1,SelectHY1,SelectHX2,SelectHY2),h);
					for(int i=0,j=h.size();i<j;i++)
					{
						HexMngr.GetHexTrack(h[i].first,h[i].second)=1;
					}
				}
			}
			else if(CurMode==CUR_MODE_MOVE)
			{
				int offs_hx=(int)SelectHX2-(int)SelectHX1;
				int offs_hy=(int)SelectHY2-(int)SelectHY1;

				SelectHX1=SelectHX2;
				SelectHY1=SelectHY2;

				SelectMove(offs_hx,offs_hy);
			}

			HexMngr.RefreshMap();
		}

		return;
	}
	else if(IntHold==INT_MAIN)
	{
		IntX=CurX-IntVectX;
		IntY=CurY-IntVectY;
	}
	else if(IntHold==INT_OBJECT)
	{
		ObjX=CurX-ItemVectX;
		ObjY=CurY-ItemVectY;
	}
}

void FOMapper::IntSetMode(int mode)
{
	CurItemProtos=NULL;
	CurProtoScroll=NULL;
	InContObject=NULL;

	switch(mode)
	{
	case INT_MODE_NONE: break;
	case INT_MODE_ARMOR: CurItemProtos=&ItemMngr.GetProtos(ITEM_TYPE_ARMOR); CurProtoScroll=&ProtoScroll[INT_MODE_ARMOR]; break;
	case INT_MODE_DRUG: CurItemProtos=&ItemMngr.GetProtos(ITEM_TYPE_DRUG); CurProtoScroll=&ProtoScroll[INT_MODE_DRUG]; break;
	case INT_MODE_WEAPON: CurItemProtos=&ItemMngr.GetProtos(ITEM_TYPE_WEAPON); CurProtoScroll=&ProtoScroll[INT_MODE_WEAPON]; break;
	case INT_MODE_AMMO: CurItemProtos=&ItemMngr.GetProtos(ITEM_TYPE_AMMO); CurProtoScroll=&ProtoScroll[INT_MODE_AMMO]; break;
	case INT_MODE_MISC: CurItemProtos=&ItemMngr.GetProtos(ITEM_TYPE_MISC); CurProtoScroll=&ProtoScroll[INT_MODE_MISC]; break;
	case INT_MODE_MISC2: CurItemProtos=&ItemMngr.GetProtos(ITEM_TYPE_MISC_EX); CurProtoScroll=&ProtoScroll[INT_MODE_MISC2]; break;
	case INT_MODE_KEY: CurItemProtos=&ItemMngr.GetProtos(ITEM_TYPE_KEY); CurProtoScroll=&ProtoScroll[INT_MODE_KEY]; break;
	case INT_MODE_CONT: CurItemProtos=&ItemMngr.GetProtos(ITEM_TYPE_CONTAINER); CurProtoScroll=&ProtoScroll[INT_MODE_CONT]; break;
	case INT_MODE_DOOR: CurItemProtos=&ItemMngr.GetProtos(ITEM_TYPE_DOOR); CurProtoScroll=&ProtoScroll[INT_MODE_DOOR]; break;
	case INT_MODE_GRID: CurItemProtos=&ItemMngr.GetProtos(ITEM_TYPE_GRID); CurProtoScroll=&ProtoScroll[INT_MODE_GRID]; break;
	case INT_MODE_GENERIC: CurItemProtos=&ItemMngr.GetProtos(ITEM_TYPE_GENERIC); CurProtoScroll=&ProtoScroll[INT_MODE_GENERIC]; break;
	case INT_MODE_WALL: CurItemProtos=&ItemMngr.GetProtos(ITEM_TYPE_WALL); CurProtoScroll=&ProtoScroll[INT_MODE_WALL]; break;
	case INT_MODE_TILE: CurProtoScroll=&ProtoScroll[INT_MODE_TILE]; break;
	case INT_MODE_CRIT: CurProtoScroll=&ProtoScroll[INT_MODE_CRIT]; break;
	case INT_MODE_FAST: CurItemProtos=&FastItemProto; CurProtoScroll=&ProtoScroll[INT_MODE_FAST]; break;
	case INT_MODE_IGNORE: CurItemProtos=&IgnoreItemProto; CurProtoScroll=&ProtoScroll[INT_MODE_IGNORE]; break;
	case INT_MODE_INCONT: InContScroll=0; InContObject=NULL; break;
	case INT_MODE_MESS:
	default: break;
	}

	IntMode=mode;
	IntHold=INT_NONE;
}

MapObject* FOMapper::FindMapObject(ProtoMap& pmap, WORD hx, WORD hy, BYTE mobj_type, WORD pid, DWORD skip)
{
	for(int i=0,j=pmap.MObjects.size();i<j;i++)
	{
		MapObject* mo=CurProtoMap->MObjects[i];

		if(mo->MapX==hx && mo->MapY==hy && mo->MapObjType==mobj_type &&
			(!pid || mo->ProtoId==pid) && !(mo->MapObjType==MAP_OBJECT_ITEM && mo->MItem.InContainer))
		{
			if(skip) skip--;
			else return mo;
		}
	}
	return NULL;
}

void FOMapper::FindMapObjects(ProtoMap& pmap, WORD hx, WORD hy, DWORD radius, BYTE mobj_type, WORD pid, MapObjectPtrVec& objects)
{
	for(int i=0,j=pmap.MObjects.size();i<j;i++)
	{
		MapObject* mo=CurProtoMap->MObjects[i];

		if(mo->MapObjType==mobj_type && DistGame(mo->MapX,mo->MapY,hx,hy)<=radius &&
			(!pid || mo->ProtoId==pid) && !(mo->MapObjType==MAP_OBJECT_ITEM && mo->MItem.InContainer))
		{
			objects.push_back(mo);
		}
	}
}

MapObject* FOMapper::FindMapObject(WORD hx, WORD hy, BYTE mobj_type, WORD pid, bool skip_selected)
{
	for(int i=0,j=CurProtoMap->MObjects.size();i<j;i++)
	{
		MapObject* mo=CurProtoMap->MObjects[i];

		if(mo->MapX==hx && mo->MapY==hy && mo->MapObjType==mobj_type && mo->ProtoId==pid &&
			!(mo->MapObjType==MAP_OBJECT_ITEM && mo->MItem.InContainer) &&
			(!skip_selected || std::find(SelectedObj.begin(),SelectedObj.end(),mo)==SelectedObj.end()))
		{
			return mo;
		}
	}
	return NULL;
}

void FOMapper::UpdateMapObject(MapObject* mobj)
{
	if(!mobj->RunTime.MapObjId) return;

	if(mobj->MapObjType==MAP_OBJECT_CRITTER)
	{
		CritterCl* cr=HexMngr.GetCritter(mobj->RunTime.MapObjId);
		if(cr) HexMngr.AffectCritter(mobj,cr);
	}
	else if(mobj->MapObjType==MAP_OBJECT_ITEM || mobj->MapObjType==MAP_OBJECT_SCENERY)
	{
		ItemHex* item=HexMngr.GetItemById(mobj->RunTime.MapObjId);
		if(item) HexMngr.AffectItem(mobj,item);
	}
}

void FOMapper::MoveMapObject(MapObject* mobj, WORD hx, WORD hy)
{
	if(hx>=HexMngr.GetMaxHexX() || hy>=HexMngr.GetMaxHexY()) return;
	if(!mobj->RunTime.MapObjId) return;

	if(Self->CurProtoMap==mobj->RunTime.FromMap)
	{
		if(mobj->MapObjType==MAP_OBJECT_CRITTER)
		{
			CritterCl* cr=HexMngr.GetCritter(mobj->RunTime.MapObjId);
			if(cr && (cr->IsDead() || !HexMngr.GetField(hx,hy).Crit))
			{
				HexMngr.RemoveCrit(cr);
				cr->HexX=hx;
				cr->HexY=hy;
				mobj->MapX=hx;
				mobj->MapY=hy;
				HexMngr.SetCrit(cr);
			}
		}
		else if(mobj->MapObjType==MAP_OBJECT_ITEM || mobj->MapObjType==MAP_OBJECT_SCENERY)
		{
			ItemHex* item=HexMngr.GetItemById(mobj->RunTime.MapObjId);
			if(item)
			{
				HexMngr.DeleteItem(item,false);
				item->HexX=hx;
				item->HexY=hy;
				mobj->MapX=hx;
				mobj->MapY=hy;
				HexMngr.PushItem(item);
			}
		}
	}
	else if(hx<mobj->RunTime.FromMap->Header.MaxHexX && hy<mobj->RunTime.FromMap->Header.MaxHexY)
	{
		if(mobj->MapObjType==MAP_OBJECT_CRITTER && mobj->MCritter.Cond!=COND_DEAD)
		{
			for(MapObjectPtrVecIt it=mobj->RunTime.FromMap->MObjects.begin();it!=mobj->RunTime.FromMap->MObjects.end();++it)
			{
				MapObject* mobj_=*it;
				if(mobj_->MapObjType==MAP_OBJECT_CRITTER && mobj_->MCritter.Cond!=COND_DEAD &&
					mobj_->MapX==hx && mobj_->MapY==hy) return;
			}
		}

		mobj->MapX=hx;
		mobj->MapY=hy;
	}
}

void FOMapper::DeleteMapObject(MapObject* mobj)
{
	ProtoMap* pmap=mobj->RunTime.FromMap;
	if(!pmap) return;

	SelectClear();
	if(!(mobj->MapObjType==MAP_OBJECT_ITEM && mobj->MItem.InContainer))
	{
		if(mobj->MapObjType==MAP_OBJECT_CRITTER || mobj->MapObjType==MAP_OBJECT_ITEM)
		{
			for(MapObjectPtrVecIt it=pmap->MObjects.begin();it!=pmap->MObjects.end();)
			{
				MapObject* mobj_=*it;
				if(mobj_->MapObjType==MAP_OBJECT_ITEM && mobj_->MItem.InContainer && mobj_->MapX==mobj->MapX && mobj_->MapY==mobj->MapY)
				{
					mobj_->Release();
					it=pmap->MObjects.erase(it);
				}
				else ++it;
			}
		}
	}

	if(pmap==CurProtoMap && mobj->RunTime.MapObjId)
	{
		if(mobj->MapObjType==MAP_OBJECT_CRITTER)
		{
			CritterCl* cr=HexMngr.GetCritter(mobj->RunTime.MapObjId);
			if(cr) HexMngr.EraseCrit(cr->GetId());
		}
		else if(mobj->MapObjType==MAP_OBJECT_ITEM || mobj->MapObjType==MAP_OBJECT_SCENERY)
		{
			ItemHex* item=HexMngr.GetItemById(mobj->RunTime.MapObjId);
			if(item) HexMngr.FinishItem(item->GetId(),0);
		}
	}

	MapObjectPtrVecIt it=std::find(pmap->MObjects.begin(),pmap->MObjects.end(),mobj);
	if(it!=pmap->MObjects.end()) pmap->MObjects.erase(it);
	mobj->Release();
}

void FOMapper::SelectClear()
{
	if(!CurProtoMap) return;

	for(size_t i=0,j=SelectedObj.size();i<j;i++)
	{
		SelMapObj* obj=&SelectedObj[i];
		if(obj->IsItem()) obj->MapItem->RestoreAlpha();
		else if(obj->IsNpc()) obj->MapNpc->Alpha=0xFF;
	}

	for(size_t i=0,j=SelectedTile.size();i<j;i++)
	{
		SelMapTile* obj=&SelectedTile[i];
		if(!obj->IsRoof) CurProtoMap->SetTile(obj->TileX,obj->TileY,obj->PicId);
		else CurProtoMap->SetRoof(obj->TileX,obj->TileY,obj->PicId);
	}

	if(SelectedTile.size()) HexMngr.ParseSelTiles();
	SelectedObj.clear();
	SelectedTile.clear();
}

void FOMapper::SelectAddItem(ItemHex* item)
{
	if(!item) return;
	MapObject* mobj=FindMapObject(item->GetHexX(),item->GetHexY(),MAP_OBJECT_ITEM,item->GetProtoId(),true);
	if(!mobj) mobj=FindMapObject(item->GetHexX(),item->GetHexY(),MAP_OBJECT_SCENERY,item->GetProtoId(),true);
	if(!mobj) return;
	SelectAdd(mobj);
}

void FOMapper::SelectAddCrit(CritterCl* npc)
{
	if(!npc) return;
	MapObject* mobj=FindMapObject(npc->GetHexX(),npc->GetHexY(),MAP_OBJECT_CRITTER,npc->Flags,true);
	if(!mobj) return;
	SelectAdd(mobj);
}

void FOMapper::SelectAddTile(WORD tx, WORD ty, DWORD name_hash, bool is_roof, bool null_old)
{
	if(tx>=HexMngr.GetMaxHexX()/2 || ty>=HexMngr.GetMaxHexY()/2) return;

	for(int i=0,j=SelectedTile.size();i<j;i++)
	{
		SelMapTile* t=&SelectedTile[i];
		if(t->TileX==tx && t->TileY==ty && t->IsRoof==is_roof) return;
	}

	if(HexMngr.GetField(tx*2,ty*2).TerrainId) return;

	DWORD spr_id=ResMngr.GetSprId(name_hash,0);
	if(!spr_id) return;

	if(!is_roof)
	{
		if(null_old)
		{
			HexMngr.GetField(tx*2,ty*2).TileId=0;
			CurProtoMap->SetTile(tx,ty,0);
		}

		HexMngr.GetField(tx*2,ty*2).SelTile=spr_id;
	}
	else
	{
		if(null_old)
		{
			HexMngr.GetField(tx*2,ty*2).RoofId=0;
			CurProtoMap->SetRoof(tx,ty,0);
		}

		HexMngr.GetField(tx*2,ty*2).SelRoof=spr_id;
	}

	SelectedTile.push_back(SelMapTile(tx,ty,name_hash,is_roof));
}

void FOMapper::SelectAdd(MapObject* mobj)
{
	if(mobj->RunTime.FromMap==CurProtoMap && mobj->RunTime.MapObjId)
	{
		if(mobj->MapObjType==MAP_OBJECT_CRITTER)
		{
			CritterCl* cr=HexMngr.GetCritter(mobj->RunTime.MapObjId);
			if(cr)
			{
				SelectedObj.push_back(SelMapObj(mobj,cr));
				cr->Alpha=SELECT_ALPHA;

				// In container
				SelMapObj* sobj=&SelectedObj[SelectedObj.size()-1];
				for(int i=0,j=CurProtoMap->MObjects.size();i<j;i++)
				{
					MapObject* mobj_=CurProtoMap->MObjects[i];
					if(mobj!=mobj_ && mobj_->MapObjType==MAP_OBJECT_ITEM && mobj_->MItem.InContainer &&
						mobj->MapX==mobj_->MapX && mobj->MapY==mobj_->MapY) sobj->Childs.push_back(mobj_);
				}
			}
		}
		else if(mobj->MapObjType==MAP_OBJECT_ITEM || mobj->MapObjType==MAP_OBJECT_SCENERY)
		{
			if(mobj->MapObjType==MAP_OBJECT_ITEM && mobj->MItem.InContainer) return;

			ItemHex* item=HexMngr.GetItemById(mobj->RunTime.MapObjId);
			if(item)
			{
				SelectedObj.push_back(SelMapObj(mobj,item));
				item->Alpha=SELECT_ALPHA;

				// In container
				if(item->Proto->GetType()!=ITEM_TYPE_CONTAINER) return;

				SelMapObj* sobj=&SelectedObj[SelectedObj.size()-1];
				for(int i=0,j=CurProtoMap->MObjects.size();i<j;i++)
				{
					MapObject* mobj1=CurProtoMap->MObjects[i];
					if(mobj!=mobj1 && mobj1->MapObjType==MAP_OBJECT_ITEM && mobj1->MItem.InContainer &&
						mobj->MapX==mobj1->MapX && mobj->MapY==mobj1->MapY) sobj->Childs.push_back(mobj1);
				}
			}
		}
	}
}

void FOMapper::SelectErase(MapObject* mobj)
{
	if(mobj->RunTime.FromMap==CurProtoMap)
	{
		if(mobj->MapObjType==MAP_OBJECT_ITEM && mobj->MItem.InContainer) return;

		for(size_t i=0,j=SelectedObj.size();i<j;i++)
		{
			SelMapObj& sobj=SelectedObj[i];
			if(sobj.MapObj==mobj)
			{
				if(mobj->RunTime.MapObjId)
				{
					if(mobj->MapObjType==MAP_OBJECT_CRITTER)
					{
						CritterCl* cr=HexMngr.GetCritter(mobj->RunTime.MapObjId);
						if(cr) cr->Alpha=0xFF;
					}
					else if(mobj->MapObjType==MAP_OBJECT_ITEM || mobj->MapObjType==MAP_OBJECT_SCENERY)
					{
						ItemHex* item=HexMngr.GetItemById(mobj->RunTime.MapObjId);
						if(item) item->RestoreAlpha();
					}
				}

				SelectedObj.erase(SelectedObj.begin()+i);
				break;
			}
		}
	}
}

void FOMapper::SelectAll()
{
	SelectClear();

	for(int i=0;i<HexMngr.GetMaxHexX()/2;i++)
	{
		for(int j=0;j<HexMngr.GetMaxHexY()/2;j++)
		{
			if(IsSelectTile && OptShowTile) SelectAddTile(i,j,CurProtoMap->GetTile(i,j),false,true);
			if(IsSelectRoof && OptShowRoof) SelectAddTile(i,j,CurProtoMap->GetRoof(i,j),true,true);
		}
	}

	ItemHexVec& items=HexMngr.GetItems();
	for(int i=0;i<items.size();i++)
	{
		if(HexMngr.IsIgnorePid(items[i]->GetProtoId())) continue;

		if(items[i]->IsItem() && IsSelectItem && OptShowItem) SelectAddItem(items[i]);
		else if(items[i]->IsScenOrGrid() && IsSelectScen && OptShowScen) SelectAddItem(items[i]);
		else if(items[i]->IsWall() && IsSelectWall && OptShowWall) SelectAddItem(items[i]);
	}

	if(IsSelectCrit && OptShowCrit)
	{
		CritMap& crits=HexMngr.GetCritters();
		for(CritMapIt it=crits.begin(),end=crits.end();it!=end;++it) SelectAddCrit((*it).second);
	}

	HexMngr.RefreshMap();
}

void FOMapper::SelectMove(int vect_hx, int vect_hy)
{
	for(size_t i=0,j=SelectedObj.size();i<j;i++)
	{
		SelMapObj* obj=&SelectedObj[i];

		int hx=obj->MapObj->MapX;
		int hy=obj->MapObj->MapY;
		hx+=vect_hx;
		hy+=vect_hy;

		if(hx<0) hx=0;
		if(hx>=HexMngr.GetMaxHexX()) hx=HexMngr.GetMaxHexX()-1;
		if(hy<0) hy=0;
		if(hy>=HexMngr.GetMaxHexY()) hy=HexMngr.GetMaxHexY()-1;

		obj->MapObj->MapX=hx;
		obj->MapObj->MapY=hy;

		if(obj->IsItem())
		{
			HexMngr.DeleteItem(obj->MapItem,false);
			obj->MapItem->HexX=hx;
			obj->MapItem->HexY=hy;
			HexMngr.PushItem(obj->MapItem);
		}
		else if(obj->IsNpc())
		{
			HexMngr.RemoveCrit(obj->MapNpc);
			obj->MapNpc->HexX=hx;
			obj->MapNpc->HexY=hy;
			HexMngr.SetCrit(obj->MapNpc);
		}
		else
		{
			continue;
		}

		for(int k=0,m=obj->Childs.size();k<m;k++)
		{
			MapObject* mobj=obj->Childs[k];
			mobj->MapX=hx;
			mobj->MapY=hy;
		}
	}

	bool is_clear=false;
	for(size_t i=0,j=SelectedTile.size();i<j;i++)
	{
		SelMapTile* t=&SelectedTile[i];

		int hx=t->HexX;
		int hy=t->HexY;
		hx+=vect_hx;
		hy+=vect_hy;

		if(hx<0) hx=0;
		if(hx>=HexMngr.GetMaxHexX()) hx=HexMngr.GetMaxHexX()-1;
		if(hy<0) hy=0;
		if(hy>=HexMngr.GetMaxHexY()) hy=HexMngr.GetMaxHexY()-1;

		t->HexX=hx;
		t->HexY=hy;
		WORD new_tx=t->HexX/2;
		WORD new_ty=t->HexY/2;	

		if(t->TileX!=new_tx || t->TileY!=new_ty)
		{
			if(!is_clear)
			{
				HexMngr.ClearSelTiles();
				is_clear=true;
			}

			DWORD spr_id=ResMngr.GetSprId(t->PicId,0);

			if(!t->IsRoof) HexMngr.GetField(new_tx*2,new_ty*2).SelTile=spr_id;
			else HexMngr.GetField(new_tx*2,new_ty*2).SelRoof=spr_id;

			t->TileX=new_tx;
			t->TileY=new_ty;
		}
	}
}

void FOMapper::SelectDelete()
{
	if(!CurProtoMap) return;

	for(size_t i=0,j=SelectedObj.size();i<j;i++)
	{
		SelMapObj* o=&SelectedObj[i];

		MapObjectPtrVecIt it=std::find(CurProtoMap->MObjects.begin(),CurProtoMap->MObjects.end(),o->MapObj);
		if(it!=CurProtoMap->MObjects.end())
		{
			SAFEREL(o->MapObj);
			CurProtoMap->MObjects.erase(it);
		}

		if(o->IsItem()) HexMngr.FinishItem(o->MapItem->GetId(),0);
		else if(o->IsNpc()) HexMngr.EraseCrit(o->MapNpc->GetId());

		for(int k=0,l=o->Childs.size();k<l;k++)
		{
			it=std::find(CurProtoMap->MObjects.begin(),CurProtoMap->MObjects.end(),o->Childs[k]);
			if(it!=CurProtoMap->MObjects.end())
			{
				SAFEREL(o->Childs[k]);
				CurProtoMap->MObjects.erase(it);
			}
		}
	}

	SelectedObj.clear();
	SelectedTile.clear();
	HexMngr.ClearSelTiles();
	SelectClear();
	HexMngr.RefreshMap();
	IntHold=INT_NONE;
	CurMode=CUR_MODE_DEF;
}

void FOMapper::ParseProto(WORD pid, WORD hx, WORD hy, bool in_cont)
{
	ProtoItem* proto_item=ItemMngr.GetProtoItem(pid);
	if(!proto_item) return;
	if(in_cont && !proto_item->IsCanPickUp()) return;
	if(hx>=HexMngr.GetMaxHexX() || hy>=HexMngr.GetMaxHexY()) return;

	AnyFrames* anim=ResMngr.GetAnim(proto_item->PicMapHash,0);
	if(!anim) return;

	SelectClear();

	if(proto_item->IsItem() || proto_item->IsScen() || proto_item->IsGrid() || proto_item->IsWall())
	{
		MapObject* mobj=new MapObject();
		mobj->RunTime.FromMap=CurProtoMap;

		mobj->MapObjType=MAP_OBJECT_ITEM;
		if(proto_item->IsScen() || proto_item->IsGrid() || proto_item->IsWall())
		{
			if(in_cont)
			{
				SAFEREL(mobj);
				return;
			}
			mobj->MapObjType=MAP_OBJECT_SCENERY;
		}

		mobj->ProtoId=pid;
		mobj->MapX=hx;
		mobj->MapY=hy;
		mobj->Dir=proto_item->Dir;
		mobj->LightDistance=proto_item->LightDistance;
		mobj->LightIntensity=proto_item->LightIntensity;
		if(in_cont) mobj->MItem.InContainer=in_cont;
		CurProtoMap->MObjects.push_back(mobj);

		if(in_cont) return;

		mobj->RunTime.MapObjId=++AnyId;
		if(HexMngr.AddItem(AnyId,pid,hx,hy,0,NULL)) SelectAdd(mobj);
	}

	HexMngr.RefreshMap();
	CurMode=CUR_MODE_DEF;
}

void FOMapper::ParseTile(DWORD name_hash, WORD hx, WORD hy)
{
	if(hx>=HexMngr.GetMaxHexX() || hy>=HexMngr.GetMaxHexY()) return;

	SelectClear();

	if(HexMngr.GetField(hx,hy).TerrainId)
	{
		HexMngr.GetField(hx,hy).TerrainId=0;
		HexMngr.RebuildTerrain();
	}

	if(hx%2) hx--;
	if(hy%2) hy--;
	HexMngr.SetTile(hx,hy,name_hash,DrawRoof);
	CurMode=CUR_MODE_DEF;
}

void FOMapper::ParseNpc(WORD pid, WORD hx, WORD hy)
{
	CritData* pnpc=CrMngr.GetProto(pid);
	if(!pnpc) return;

	if(hx>=HexMngr.GetMaxHexX() || hy>=HexMngr.GetMaxHexY()) return;
	if(HexMngr.GetField(hx,hy).Crit) return;

	AnyFrames* anim=ResMngr.GetCritAnim(pnpc->BaseType,1,1,NpcDir);
	DWORD spr_id=(anim?anim->GetSprId(0):0);
	if(!spr_id) return;

	SelectClear();

	MapObject* mobj=new MapObject();
	mobj->RunTime.FromMap=CurProtoMap;
	mobj->RunTime.MapObjId=++AnyId;
	mobj->MapObjType=MAP_OBJECT_CRITTER;
	mobj->ProtoId=pid;
	mobj->MapX=hx;
	mobj->MapY=hy;
	mobj->Dir=NpcDir;
	for(int i=0;i<MAPOBJ_CRITTER_PARAMS;i++) mobj->MCritter.ParamIndex[i]=DefaultCritterParam[i];
	CurProtoMap->MObjects.push_back(mobj);

	CritterCl* cr=new CritterCl();
	cr->SetBaseType(pnpc->BaseType);
	cr->DefItemSlotMain.Init(ItemMngr.GetProtoItem(ITEM_DEF_SLOT));
	cr->DefItemSlotExt.Init(ItemMngr.GetProtoItem(ITEM_DEF_SLOT));
	cr->DefItemSlotArmor.Init(ItemMngr.GetProtoItem(ITEM_DEF_ARMOR));
	cr->HexX=hx;
	cr->HexY=hy;
	cr->SetDir(NpcDir);
	cr->Cond=COND_LIFE;
	cr->CondExt=COND_LIFE_NONE;
	cr->Flags=pid;
	memcpy(cr->Params,pnpc->Params,sizeof(pnpc->Params));
	cr->Id=AnyId;
	cr->Init();

	HexMngr.AddCrit(cr);
	SelectAdd(mobj);

	HexMngr.RefreshMap();
	CurMode=CUR_MODE_DEF;
}

MapObject* FOMapper::ParseMapObj(MapObject* mobj)
{
	if(mobj->MapObjType==MAP_OBJECT_CRITTER)
	{
		if(HexMngr.GetField(mobj->MapX,mobj->MapY).Crit) return NULL;
		CritData* pnpc=CrMngr.GetProto(mobj->ProtoId);
		if(!pnpc) return NULL;

		CurProtoMap->MObjects.push_back(new MapObject(*mobj));
		mobj=CurProtoMap->MObjects[CurProtoMap->MObjects.size()-1];
		mobj->RunTime.FromMap=CurProtoMap;
		mobj->RunTime.MapObjId=++AnyId;

		CritterCl* cr=new CritterCl();
		cr->SetBaseType(pnpc->BaseType);
		cr->DefItemSlotMain.Init(ItemMngr.GetProtoItem(ITEM_DEF_SLOT));
		cr->DefItemSlotExt.Init(ItemMngr.GetProtoItem(ITEM_DEF_SLOT));
		cr->DefItemSlotArmor.Init(ItemMngr.GetProtoItem(ITEM_DEF_ARMOR));
		cr->HexX=mobj->MapX;
		cr->HexY=mobj->MapY;
		cr->SetDir(mobj->Dir);
		cr->Cond=COND_LIFE;
		cr->CondExt=COND_LIFE_NONE;
		cr->Flags=mobj->ProtoId;
		memcpy(cr->Params,pnpc->Params,sizeof(pnpc->Params));
		cr->Id=AnyId;
		cr->Init();
		HexMngr.AddCrit(cr);
		SelectAdd(mobj);
	}
	else if(mobj->MapObjType==MAP_OBJECT_ITEM || mobj->MapObjType==MAP_OBJECT_SCENERY)
	{
		ProtoItem* proto=ItemMngr.GetProtoItem(mobj->ProtoId);
		if(!proto) return NULL;

		CurProtoMap->MObjects.push_back(new MapObject(*mobj));
		mobj=CurProtoMap->MObjects[CurProtoMap->MObjects.size()-1];
		mobj->RunTime.FromMap=CurProtoMap;

		if(mobj->MapObjType==MAP_OBJECT_ITEM && mobj->MItem.InContainer) return mobj;
		mobj->RunTime.MapObjId=++AnyId;
		if(HexMngr.AddItem(AnyId,mobj->ProtoId,mobj->MapX,mobj->MapY,0,NULL)) SelectAdd(mobj);
	}
	return mobj;
}

void FOMapper::BufferCopy()
{
	if(!CurProtoMap) return;

	MapObjBuffer.clear();
	TilesBuffer.clear();

	for(int i=0,j=SelectedObj.size();i<j;i++)
	{
		MapObjBuffer.push_back(new MapObject(*SelectedObj[i].MapObj));
		for(int k=0,l=SelectedObj[i].Childs.size();k<l;k++)
			MapObjBuffer.push_back(new MapObject(*SelectedObj[i].Childs[k]));
	}

	for(int i=0,j=SelectedTile.size();i<j;i++)
	{
		TilesBuffer.push_back(SelectedTile[i]);
	}
}

void FOMapper::BufferCut()
{
	if(!CurProtoMap) return;

	BufferCopy();
	SelectDelete();
}

void FOMapper::BufferPaste(int hx, int hy)
{
	if(!CurProtoMap) return;

	SelectClear();
	CurProtoMap->MObjects.reserve(CurProtoMap->MObjects.size()+MapObjBuffer.size()*2);

	for(int i=0,j=MapObjBuffer.size();i<j;i++)
	{
		ParseMapObj(MapObjBuffer[i]);
	}

	for(int i=0,j=TilesBuffer.size();i<j;i++)
	{
		SelMapTile* t=&TilesBuffer[i];
		SelectAddTile(t->TileX,t->TileY,t->PicId,t->IsRoof,false);	
	}
}

void FOMapper::CurDraw()
{
	DWORD spr_id;
	switch(CurMode)
	{
	case CUR_MODE_DEF: spr_id=CurPDef; break;
	case CUR_MODE_MOVE: spr_id=CurPHand; break;
	case CUR_MODE_DRAW:
		if(IsObjectMode() && (*CurItemProtos).size())
		{
			ProtoItem& proto_item=(*CurItemProtos)[CurProto[IntMode]];
			spr_id=ResMngr.GetSprId(proto_item.PicMapHash,proto_item.Dir);
			if(!spr_id) spr_id=ItemHex::DefaultAnim->GetSprId(0);

			WORD hx,hy;
			if(!HexMngr.GetHexPixel(CurX,CurY,hx,hy)) return;

			SpriteInfo* si=SprMngr.GetSpriteInfo(spr_id);
			int x=HexMngr.GetField(hx,hy).ScrX-(si->Width/2)+si->OffsX;
			int y=HexMngr.GetField(hx,hy).ScrY-si->Height+si->OffsY;

			SprMngr.DrawSpriteSize(spr_id,(x+16+CmnScrOx)/SpritesZoom,(y+CmnScrOy+6)/SpritesZoom,si->Width/SpritesZoom,si->Height/SpritesZoom,true,false);
		}
		else if(IsTileMode() && TilesPictures.size())
		{
			spr_id=ResMngr.GetSprId(TilesPictures[CurProto[IntMode]],0);
			if(!spr_id) spr_id=ItemHex::DefaultAnim->GetSprId(0);

			WORD hx,hy;
			if(!HexMngr.GetHexPixel(CurX,CurY,hx,hy)) return;
			if(hx%2) hx--;
			if(hy%2) hy--;

			SpriteInfo* si=SprMngr.GetSpriteInfo(spr_id);
			int x=HexMngr.GetField(hx,hy).ScrX-(si->Width/2)+si->OffsX;
			int y=HexMngr.GetField(hx,hy).ScrY-si->Height+si->OffsY;
			x=x-16-8;
			y=y-6+32;
			if(DrawRoof) y-=98;

			SprMngr.DrawSpriteSize(spr_id,(x+16+CmnScrOx)/SpritesZoom,(y+CmnScrOy+6)/SpritesZoom,si->Width/SpritesZoom,si->Height/SpritesZoom,true,false);
		}
		else if(IsCritMode() && NpcProtos.size())
		{
			AnyFrames* anim=ResMngr.GetCritAnim(NpcProtos[CurProto[IntMode]]->BaseType,1,1,NpcDir);
			spr_id=(anim?anim->GetSprId(0):0);
			if(!spr_id) spr_id=ItemHex::DefaultAnim->GetSprId(0);

			WORD hx,hy;
			if(!HexMngr.GetHexPixel(CurX,CurY,hx,hy)) return;

			SpriteInfo* si=SprMngr.GetSpriteInfo(spr_id);
			int x=HexMngr.GetField(hx,hy).ScrX-(si->Width/2)+si->OffsX;
			int y=HexMngr.GetField(hx,hy).ScrY-si->Height+si->OffsY;

			SprMngr.DrawSpriteSize(spr_id,(x+CmnScrOx+16)/SpritesZoom,(y+CmnScrOy+6)/SpritesZoom,si->Width/SpritesZoom,si->Height/SpritesZoom,true,false);
		}
		else
		{
			CurMode=CUR_MODE_DEF;
		}
		return;
	default:
		CurMode=CUR_MODE_DEF;
		return;
	}

	SpriteInfo* si=SprMngr.GetSpriteInfo(spr_id);
	int x=CurX-(si->Width/2)+si->OffsX;
	int y=CurY-si->Height+si->OffsY;
	SprMngr.DrawSprite(spr_id,x,y,COLOR_IFACE);
}

void FOMapper::CurRMouseUp()
{
	if(IntHold==INT_NONE)
	{
		if(CurMode==CUR_MODE_MOVE)
		{
			/*if(SelectedTile.size())
			{
				for(int i=0,j=SelectedTile.size();i<j;i++)
				{
					SelMapTile* t=&SelectedTile[i];

					if(!t->IsRoof) CurProtoMap->Tiles.SetTile(t->TileX,t->TileY,t->Pid);
					else CurProtoMap->SetRoof(t->TileX,t->TileY,t->Pid);
				}

				HexMngr.ParseSelTiles();
				HexMngr.RefreshMap();
			}

			SelectClear();*/
			CurMode=CUR_MODE_DEF;
		}
		else if(CurMode==CUR_MODE_DRAW)
		{
			CurMode=CUR_MODE_DEF;
		}
		else if(CurMode==CUR_MODE_DEF)
		{
			if(SelectedObj.size() || SelectedTile.size()) CurMode=CUR_MODE_MOVE;
			else if(IsObjectMode() || IsTileMode() || IsCritMode()) CurMode=CUR_MODE_DRAW;
		}	
	}
}

void FOMapper::CurMMouseDown()
{
	if(SelectedObj.empty())
	{
		NpcDir++;
		if(NpcDir>5) NpcDir=0;

		DrawRoof=!DrawRoof;
	}
	else
	{
		for(int i=0,j=SelectedObj.size();i<j;i++)
		{
			SelMapObj* o=&SelectedObj[i];

			if(o->IsNpc())
			{
				int dir=o->MapNpc->GetDir()+1;
				if(dir>5) dir=0;
				o->MapNpc->SetDir(dir);
				o->MapObj->Dir=dir;
			}
		}
	}
}

bool FOMapper::GetMouseHex(WORD& hx, WORD& hy)
{
	hx=hy=0;
	if(IntVisible && IsCurInRectNoTransp(IntMainPic,IntWMain,IntX,IntY)) return false;
	if(ObjVisible && !SelectedObj.empty() && IsCurInRectNoTransp(ObjWMainPic,ObjWMain,0,0)) return false;

	//if(ConsoleEdit && IsCurInRectNoTransp(ConsolePic,Main,0,0)) //TODO:
	return HexMngr.GetHexPixel(CurX,CurY,hx,hy);
}

void FOMapper::ConsoleDraw()
{
	if(ConsoleEdit) SprMngr.DrawSprite(ConsolePic,IntX+ConsolePicX,(IntVisible?IntY:MODE_HEIGHT)+ConsolePicY);

	SprMngr.Flush();

	if(ConsoleEdit)
	{
		char str_to_edit[2048];
		static bool show_cur=true;
		static DWORD show_cur_last_time=Timer::FastTick();

		if(Timer::FastTick()>show_cur_last_time+400)
		{
			show_cur=!show_cur;
			show_cur_last_time=Timer::FastTick();
		}

		str_to_edit[0]=0;
		StringCopy(str_to_edit,ConsoleStr);
		if(show_cur)
			str_to_edit[ConsoleCur]='!';
		else
			str_to_edit[ConsoleCur]='.';

		str_to_edit[ConsoleCur+1]=0;
		StringAppend(str_to_edit,&ConsoleStr[ConsoleCur]);
		SprMngr.DrawStr(INTRECT(IntX+ConsoleTextX,(IntVisible?IntY:MODE_HEIGHT)+ConsoleTextY,MODE_WIDTH,MODE_HEIGHT),str_to_edit,FT_NOBREAK);
	}
}

void FOMapper::ConsoleKeyDown(BYTE dik)
{
	if(dik==DIK_RETURN || dik==DIK_NUMPADENTER)
	{
		if(ConsoleEdit)
		{
			if(!ConsoleStr[0])
			{
				ConsoleEdit=false;
			}
			else
			{
				ConsoleHistory.push_back(string(ConsoleStr));
				for(int i=0;i<ConsoleHistory.size()-1;i++)
				{
					if(ConsoleHistory[i]==ConsoleHistory[ConsoleHistory.size()-1])
					{
						ConsoleHistory.erase(ConsoleHistory.begin()+i);
						i=-1;
					}
				}
				ConsoleHistoryCur=ConsoleHistory.size();

				bool process_command=true;
				if(MapperFunctions.ConsoleMessage && Script::PrepareContext(MapperFunctions.ConsoleMessage,__FUNCTION__,"Mapper"))
				{
					CScriptString* sstr=new CScriptString(ConsoleStr);
					Script::SetArgObject(sstr);
					if(Script::RunPrepared() && Script::GetReturnedBool()) process_command=false;
					StringCopy(ConsoleStr,sstr->c_str());
					sstr->Release();
				}

				AddMess(ConsoleStr);
				if(process_command) ParseCommand(ConsoleStr);
				ConsoleStr[0]=0;
				ConsoleCur=0;
			}
		}
		else 
		{
			ConsoleEdit=true;
			ConsoleStr[0]=0;
			ConsoleCur=0;
			ConsoleHistoryCur=ConsoleHistory.size();
		}

		return;
	}

	switch(dik)
	{
	case DIK_UP:
		if(ConsoleHistoryCur-1<0) return;
		ConsoleHistoryCur--;	
		StringCopy(ConsoleStr,ConsoleHistory[ConsoleHistoryCur].c_str());
		ConsoleCur=strlen(ConsoleStr);
		return;
	case DIK_DOWN:
		if(ConsoleHistoryCur+1>=ConsoleHistory.size())
		{
			ConsoleHistoryCur=ConsoleHistory.size();
			StringCopy(ConsoleStr,"");
			ConsoleCur=0;
			return;
		}
		ConsoleHistoryCur++;
		StringCopy(ConsoleStr,ConsoleHistory[ConsoleHistoryCur].c_str());
		ConsoleCur=strlen(ConsoleStr);
		return;
	default:
		Keyb::GetChar(dik,ConsoleStr,&ConsoleCur,MAX_NET_TEXT,KIF_NO_SPEC_SYMBOLS);
		
		ConsoleLastKey=dik;
		ConsoleKeyTick=Timer::FastTick();
		ConsoleAccelerate=1;
		return;
	}
}

void FOMapper::ConsoleKeyUp(BYTE key)
{
	ConsoleLastKey=0;
}

void FOMapper::ConsoleProcess()
{
	if(!ConsoleLastKey) return;

	if(Timer::FastTick()-ConsoleKeyTick>=CONSOLE_KEY_TICK-ConsoleAccelerate)
	{
		ConsoleKeyTick=Timer::FastTick();

		ConsoleAccelerate=CONSOLE_MAX_ACCELERATE;
//		if((ConsoleAccelerate*=4)>=CONSOLE_MAX_ACCELERATE) ConsoleAccelerate=CONSOLE_MAX_ACCELERATE;

		Keyb::GetChar(ConsoleLastKey,ConsoleStr,&ConsoleCur,MAX_NET_TEXT,KIF_NO_SPEC_SYMBOLS);
	}
}

void FOMapper::ParseCommand(const char* cmd)
{
	if(!cmd || !*cmd) return;

	// Load map
	if(*cmd=='~')
	{
		cmd++;
		char map_name[256];
		Str::CopyWord(map_name,cmd,' ',false);
		if(!map_name[0])
		{
			AddMess("Error parse map name.");
			return;
		}

		ProtoMap* pmap=new ProtoMap();
		if(!pmap->Init(0xFFFF,map_name,PT_MAPS))
		{
			AddMess("File not found or truncated.");
			return;
		}

		SelectClear();
		if(!HexMngr.SetProtoMap(*pmap))
		{
			AddMess("Load map fail.");
			return;
		}

		HexMngr.FindSetCenter(pmap->Header.CenterX,pmap->Header.CenterY);
		CurProtoMap=pmap;
		LoadedProtoMaps.push_back(pmap);

		AddMess("Load map complete.");
	}
	// Save map
	else if(*cmd=='^')
	{
		cmd++;

		char map_name[256];
		Str::CopyWord(map_name,cmd,' ',false);
		if(!map_name[0])
		{
			AddMess("Error parse map name.");
			return;
		}

		if(!CurProtoMap)
		{
			AddMess("Map not loaded.");
			return;
		}

		SelectClear();
		HexMngr.RefreshMap();
		if(CurProtoMap->Save(map_name,PT_MAPS,strstr(cmd,"/text")!=NULL,strstr(cmd,"/nopack")==NULL)) AddMess("Save map success.");
		else AddMess("Save map fail, see log.");
	}
	// Run script
	else if(*cmd=='#')
	{
		cmd++;
		char func_name[1024];
		char str[1024]={0};
		if(sscanf(cmd,"%s",func_name)!=1 || !strlen(func_name))
		{
			AddMess("Function name not typed.");
			return;
		}
		StringCopy(str,cmd+strlen(func_name));
		while(str[0]==' ') Str::CopyBack(str);

		// Reparse module
		int bind_id;
		if(strstr(func_name,"@"))
			bind_id=Script::Bind(func_name,"string %s(string)",true);
		else
			bind_id=Script::Bind("mapper_main",func_name,"string %s(string)",true);

		if(bind_id>0 && Script::PrepareContext(bind_id,__FUNCTION__,"Mapper"))
		{
			CScriptString* sstr=new CScriptString(str);
			Script::SetArgObject(sstr);
			if(Script::RunPrepared())
			{
				CScriptString* sstr_=(CScriptString*)Script::GetReturnedObject();
				AddMessFormat(Str::Format("Result: %s",sstr_->c_str()));
				sstr_->Release();
			}
			else
			{
				AddMess("Script execution fail.");
			}
			sstr->Release();
		}
		else
		{
			AddMess("Function not found.");
			return;
		}
	}
	// Critter animations
	else if(*cmd=='@')
	{
		AddMess("Playing critter animations.");

		if(!CurProtoMap)
		{
			AddMess("Map not loaded.");
			return;
		}

		cmd++;
		char anim[512];
		StringCopy(anim,cmd);
		Str::Lwr(anim);
		Str::EraseChars(anim,' ');
		if(!strlen(anim) || strlen(anim)%2) return;

		if(SelectedObj.size())
		{
			for(size_t i=0;i<SelectedObj.size();i++)
			{
				SelMapObj& sobj=SelectedObj[i];
				if(sobj.MapNpc)
				{
					sobj.MapNpc->ClearAnim();
					for(size_t j=0;j<strlen(anim)/2;j++) sobj.MapNpc->Animate(anim[j*2]-'a'+1,anim[j*2+1]-'a'+1,NULL);
				}
			}
		}
		else
		{
			CritMap& crits=HexMngr.GetCritters();
			for(CritMapIt it=crits.begin();it!=crits.end();++it)
			{
				CritterCl* cr=(*it).second;
				cr->ClearAnim();
				for(int j=0;j<strlen(anim)/2;j++) cr->Animate(anim[j*2]-'a'+1,anim[j*2+1]-'a'+1,NULL);
			}
		}
	}
	// Other
	else if(*cmd=='*')
	{
		if(strstr(cmd,"new"))
		{
			ProtoMap* pmap=new ProtoMap();
			pmap->GenNew(&FileMngr);

			if(!HexMngr.SetProtoMap(*pmap))
			{
				AddMess("Create map fail, see log.");
				return;
			}

			AddMess("Create map success.");
			HexMngr.FindSetCenter(150,150);

			CurProtoMap=pmap;
			LoadedProtoMaps.push_back(pmap);
		}
		else if(strstr(cmd,"unload"))
		{
			AddMess("Unload map.");

			ProtoMapPtrVec::iterator it=std::find(LoadedProtoMaps.begin(),LoadedProtoMaps.end(),CurProtoMap);
			if(it==LoadedProtoMaps.end()) return;

			LoadedProtoMaps.erase(it);
			SelectedObj.clear();
			CurProtoMap->Clear();
			SAFEREL(CurProtoMap);

			if(LoadedProtoMaps.empty())
			{
				HexMngr.UnloadMap();
				return;
			}

			CurProtoMap=LoadedProtoMaps[0];
			if(HexMngr.SetProtoMap(*CurProtoMap))
			{
				HexMngr.FindSetCenter(CurProtoMap->Header.CenterX,CurProtoMap->Header.CenterY);
				return;
			}
		}
		else if(strstr(cmd,"scripts"))
		{
			FinishScriptSystem();
			InitScriptSystem();
			AddMess("Scripts reloaded.");
		}
		else if(!CurProtoMap) return;

		if(strstr(cmd,"dupl"))
		{
			AddMess("Find duplicates.");

			for(int hx=0;hx<HexMngr.GetMaxHexX();hx++)
			{
				for(int hy=0;hy<HexMngr.GetMaxHexY();hy++)
				{
					Field& f=HexMngr.GetField(hx,hy);
					WordVec pids;
					for(ItemHexVecIt it=f.Items.begin(),end=f.Items.end();it!=end;++it) pids.push_back((*it)->GetProtoId());
					std::sort(pids.begin(),pids.end());
					for(int i=0,j=pids.size(),same=0;i<j;i++)
					{
						if(i<j-1 && pids[i]==pids[i+1]) same++;
						else
						{
							if(same) AddMessFormat("%d duplicates of %d on %d:%d.",same,pids[i],hx,hy);
							same=0;
						}
					}
				}
			}
		}
		else if(strstr(cmd,"scroll"))
		{
			AddMess("Find alone scrollblockers.");

			for(int hx=0;hx<HexMngr.GetMaxHexX();hx++)
			{
				for(int hy=0;hy<HexMngr.GetMaxHexY();hy++)
				{
					if(!HexMngr.GetField(hx,hy).ScrollBlock) continue;
					WORD hx_=hx,hy_=hy;
					int count=0;
					MoveHexByDir(hx_,hy_,4,HexMngr.GetMaxHexX(),HexMngr.GetMaxHexY());
					for(int i=0;i<5;i++)
					{
						if(HexMngr.GetField(hx_,hy_).ScrollBlock) count++;
						MoveHexByDir(hx_,hy_,i,HexMngr.GetMaxHexX(),HexMngr.GetMaxHexY());
					}
					if(count<2) AddMessFormat("Alone scrollblocker on %03d:%03d.",hx,hy);
				}
			}
		}
		else if(strstr(cmd,"pidpos"))
		{
			AddMess("Find pid positions.");

			cmd+=strlen("*pidpos");
			DWORD pid;
			if(sscanf(cmd,"%u",&pid)!=1) return;

			
			DWORD count=1;
			for(MapObjectPtrVecIt it=CurProtoMap->MObjects.begin(),end=CurProtoMap->MObjects.end();it!=end;++it)
			{
				MapObject* mobj=*it;
				if(mobj->ProtoId==pid)
				{
					AddMessFormat("%04u) %03d:%03d %s",count,mobj->MapX,mobj->MapY,mobj->MapObjType==MAP_OBJECT_ITEM && mobj->MItem.InContainer?"in container":"");
					count++;
				}
			}
		}
		else if(strstr(cmd,"size"))
		{
			AddMess("Resize map.");

			cmd+=strlen("*size");
			int maxhx,maxhy;
			if(sscanf(cmd,"%d%d",&maxhx,&maxhy)!=2)
			{
				AddMess("Invalid args.");
				return;
			}
			SelectClear();
			HexMngr.UnloadMap();
			WORD old_maxhx=CurProtoMap->Header.MaxHexX;
			WORD old_maxhy=CurProtoMap->Header.MaxHexY;
			maxhx=CLAMP(maxhx,MAXHEX_MIN,MAXHEX_MAX);
			maxhy=CLAMP(maxhy,MAXHEX_MIN,MAXHEX_MAX);
			if(CurProtoMap->Header.CenterX>=maxhx) CurProtoMap->Header.CenterX=maxhx-1;
			if(CurProtoMap->Header.CenterY>=maxhy) CurProtoMap->Header.CenterY=maxhy-1;
			CurProtoMap->Header.MaxHexX=maxhx;
			CurProtoMap->Header.MaxHexY=maxhy;

			// Delete truncated objects
			for(MapObjectPtrVecIt it=CurProtoMap->MObjects.begin();it!=CurProtoMap->MObjects.end();)
			{
				MapObject* mobj=*it;
				if(mobj->MapX>=maxhx || mobj->MapY>=maxhy)
				{
					SAFEREL(mobj);
					it=CurProtoMap->MObjects.erase(it);
				}
				else ++it;
			}

			// Reload map
			DWORD* old_tiles=CurProtoMap->Tiles;
			CurProtoMap->Tiles=new DWORD[CurProtoMap->GetTilesSize()/sizeof(DWORD)];
			ZeroMemory(CurProtoMap->Tiles,CurProtoMap->GetTilesSize());
			for(int tx=0,ex=min(old_maxhx/2,CurProtoMap->Header.MaxHexX/2);tx<ex;tx++)
			{
				for(int ty=0,ey=min(old_maxhy/2,CurProtoMap->Header.MaxHexY/2);ty<ey;ty++)
				{
					CurProtoMap->Tiles[ty*(CurProtoMap->Header.MaxHexX/2)*2+tx*2]=old_tiles[ty*(old_maxhx/2)*2+tx*2];
					CurProtoMap->Tiles[ty*(CurProtoMap->Header.MaxHexX/2)*2+tx*2+1]=old_tiles[ty*(old_maxhx/2)*2+tx*2+1];
				}
			}
			delete[] old_tiles;
			HexMngr.SetProtoMap(*CurProtoMap);
			HexMngr.FindSetCenter(CurProtoMap->Header.CenterX,CurProtoMap->Header.CenterY);
		}
		else if(strstr(cmd,"hex"))
		{
			AddMess("Show hex objects.");

			cmd+=strlen("*hex");
			int hx,hy;
			if(sscanf(cmd,"%d%d",&hx,&hy)!=2)
			{
				AddMess("Invalid args.");
				return;
			}

			// Show all objects in this hex
			DWORD count=1;
			for(MapObjectPtrVecIt it=CurProtoMap->MObjects.begin();it!=CurProtoMap->MObjects.end();++it)
			{
				MapObject* mobj=*it;
				if(mobj->MapX==hx && mobj->MapY==hy)
				{
					AddMessFormat("%04u) pid %03d %s",count,mobj->ProtoId,mobj->MapObjType==MAP_OBJECT_ITEM && mobj->MItem.InContainer?"in container":"");
					count++;
				}
			}
		}
	}
	else
	{
		AddMess("Unknown command.");
	}
}

void FOMapper::AddMess(const char* message_text)
{
	// Text
	char str[MAX_FOTEXT];
	sprintf(str,"|%u %c |%u %s\n",COLOR_TEXT,149,COLOR_TEXT,message_text);

	// Time
	SYSTEMTIME sys_time;
	GetSystemTime(&sys_time);
	char mess_time[64];
	sprintf(mess_time,"%02d:%02d:%02d ",sys_time.wHour,sys_time.wMinute,sys_time.wSecond);

	// Add
	MessBox.push_back(MessBoxMessage(0,str,mess_time));

	// Generate mess box
	MessBoxScroll=0;
	MessBoxGenerate();
}

void FOMapper::AddMessFormat(const char* message_text, ...)
{
	char str[MAX_FOTEXT];
	va_list list;
	va_start(list,message_text);
	wvsprintf(str,message_text,list);
	va_end(list);
	AddMess(str);
}

void FOMapper::MessBoxGenerate()
{
	MessBoxCurText="";
	if(MessBox.empty()) return;

	INTRECT ir(IntWWork[0]+IntX,IntWWork[1]+IntY,IntWWork[2]+IntX,IntWWork[3]+IntY);
	int max_lines=ir.H()/10;
	if(ir.IsZero()) max_lines=20;

	int cur_mess=MessBox.size()-1;
	for(int i=0,j=0;cur_mess>=0;cur_mess--)
	{
		MessBoxMessage& m=MessBox[cur_mess];
		// Scroll
		j++;
		if(j<=MessBoxScroll) continue;
		// Add to message box
		if(OptMsgboxInvert) MessBoxCurText+=m.Mess; //Back
		else MessBoxCurText=m.Mess+MessBoxCurText; //Front
		i++;
		if(i>=max_lines) break;
	}
}

void FOMapper::MessBoxDraw()
{
	if(!IntVisible) return;
	if(MessBoxCurText.empty()) return;

	DWORD flags=FT_COLORIZE;
	if(!OptMsgboxInvert) flags|=FT_UPPER|FT_BOTTOM;

	SprMngr.DrawStr(INTRECT(IntWWork[0]+IntX,IntWWork[1]+IntY,IntWWork[2]+IntX,IntWWork[3]+IntY),MessBoxCurText.c_str(),flags);
}

bool FOMapper::SaveLogFile()
{
	if(MessBox.empty()) return false;

	SYSTEMTIME sys_time;
	GetSystemTime(&sys_time);

	char log_path[512];
	sprintf(log_path,"%smapper_messbox_%02d-%02d-%d_%02d-%02d-%02d.txt",PATH_LOG_FILE,
		sys_time.wDay,sys_time.wMonth,sys_time.wYear,
		sys_time.wHour,sys_time.wMinute,sys_time.wSecond);

	HANDLE save_file=CreateFile(log_path,GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_NEW,FILE_FLAG_WRITE_THROUGH,NULL);
	if(save_file==INVALID_HANDLE_VALUE) return false;

	char cur_mess[MAX_FOTEXT];
	string fmt_log;
	for(int i=0;i<MessBox.size();++i)
	{
		StringCopy(cur_mess,MessBox[i].Mess.c_str());
		Str::EraseWords(cur_mess,'|',' ');
		fmt_log+=MessBox[i].Time+string(cur_mess);
	}

	DWORD br;
	WriteFile(save_file,fmt_log.c_str(),fmt_log.length(),&br,NULL);
	CloseHandle(save_file);
	return true;
}

void FOMapper::InitScriptSystem()
{
	WriteLog("Script system initialization...\n");

	// Init
	if(!Script::Init(false,NULL))
	{
		WriteLog("Script system initialization fail.\n");
		return;
	}

	// Bind vars and functions, look bind.h
	asIScriptEngine* engine=Script::GetEngine();
#define BIND_MAPPER
#define BIND_CLASS FOMapper::SScriptFunc::
#define BIND_ERROR do{WriteLog(__FUNCTION__" - Bind error, line<%d>.\n",__LINE__);}while(0)
#include <ScriptBind.h>

	// Load scripts
	Script::SetScriptsPath(FileManager::GetFullPath("",PT_SCRIPTS));

	// Get config file
	FileManager scripts_cfg;
	scripts_cfg.LoadFile(SCRIPTS_LST,PT_SCRIPTS);
	if(!scripts_cfg.IsLoaded())
	{
		WriteLog("Config file<%s> not found.\n",SCRIPTS_LST);
		return;
	}

	// Load script modules
	Script::Undefine(NULL);
	Script::Define("__MAPPER");
	Script::ReloadScripts((char*)scripts_cfg.GetBuf(),"mapper",GameOpt.SkipScriptBinaries);

	// Bind game functions
	ReservedScriptFunction BindGameFunc[]={
		{&MapperFunctions.Start,"start","void %s()"},
		{&MapperFunctions.Loop,"loop","uint %s()"},
		{&MapperFunctions.ConsoleMessage,"console_message","bool %s(string&)"},
		{&MapperFunctions.RenderIface,"render_iface","void %s(uint)"},
		{&MapperFunctions.RenderMap,"render_map","void %s()"},
		{&MapperFunctions.MouseDown,"mouse_down","bool %s(int)"},
		{&MapperFunctions.MouseUp,"mouse_up","bool %s(int)"},
		{&MapperFunctions.MouseMove,"mouse_move","void %s(int,int)"},
		{&MapperFunctions.KeyDown,"key_down","bool %s(uint8)"},
		{&MapperFunctions.KeyUp,"key_up","bool %s(uint8)"},
		{&MapperFunctions.InputLost,"input_lost","void %s()"},
	};
	Script::BindReservedFunctions((char*)scripts_cfg.GetBuf(),"mapper",BindGameFunc,sizeof(BindGameFunc)/sizeof(BindGameFunc[0]));

	if(MapperFunctions.Start && Script::PrepareContext(MapperFunctions.Start,__FUNCTION__,"Mapper")) Script::RunPrepared();
	WriteLog("Script system initialization complete.\n");
}

void FOMapper::FinishScriptSystem()
{
	Script::Finish();
	ZeroMemory(&MapperFunctions,sizeof(MapperFunctions));
}

void FOMapper::DrawIfaceLayer(DWORD layer)
{
	if(MapperFunctions.RenderIface && Script::PrepareContext(MapperFunctions.RenderIface,__FUNCTION__,"Mapper"))
	{
		SpritesCanDraw=true;
		Script::SetArgDword(layer);
		Script::RunPrepared();
		SpritesCanDraw=false;
	}
}

#define SCRIPT_ERROR(error) do{ScriptLastError=error; Script::LogError(__FUNCTION__", "error);}while(0)
#define SCRIPT_ERROR_RX(error,x) do{ScriptLastError=error; Script::LogError(__FUNCTION__", "error); return x;}while(0)
#define SCRIPT_ERROR_R(error) do{ScriptLastError=error; Script::LogError(__FUNCTION__", "error); return;}while(0)
#define SCRIPT_ERROR_R0(error) do{ScriptLastError=error; Script::LogError(__FUNCTION__", "error); return 0;}while(0)
static string ScriptLastError;

CScriptString* FOMapper::SScriptFunc::MapperObject_get_ScriptName(MapObject& mobj)
{
	return new CScriptString(mobj.ScriptName);
}

void FOMapper::SScriptFunc::MapperObject_set_ScriptName(MapObject& mobj, CScriptString* str)
{
	StringCopy(mobj.ScriptName,str?str->c_str():NULL);
}

CScriptString* FOMapper::SScriptFunc::MapperObject_get_FuncName(MapObject& mobj)
{
	return new CScriptString(mobj.FuncName);
}

void FOMapper::SScriptFunc::MapperObject_set_FuncName(MapObject& mobj, CScriptString* str)
{
	StringCopy(mobj.FuncName,str?str->c_str():NULL);
}

BYTE FOMapper::SScriptFunc::MapperObject_get_Critter_Cond(MapObject& mobj)
{
	if(!mobj.MapObjType!=MAP_OBJECT_CRITTER) SCRIPT_ERROR_R0("Map object is not critter.");
	return mobj.MCritter.Cond;
}

void FOMapper::SScriptFunc::MapperObject_set_Critter_Cond(MapObject& mobj, BYTE value)
{
	if(!mobj.MapObjType!=MAP_OBJECT_CRITTER) SCRIPT_ERROR_R("Map object is not critter.");
	if(!mobj.RunTime.FromMap) return;
	if(value<COND_LIFE || value>COND_DEAD) return;
	if(mobj.MCritter.Cond==COND_DEAD && value!=COND_LIFE)
	{
		for(MapObjectPtrVecIt it=mobj.RunTime.FromMap->MObjects.begin();it!=mobj.RunTime.FromMap->MObjects.end();++it)
		{
			MapObject* mobj_=*it;
			if(mobj_->MapObjType==MAP_OBJECT_CRITTER && mobj_->MCritter.Cond!=COND_DEAD && mobj_->MapX==mobj.MapX && mobj_->MapY==mobj.MapY) return;
		}
	}
	mobj.MCritter.Cond=value;
}

CScriptString* FOMapper::SScriptFunc::MapperObject_get_PicMap(MapObject& mobj)
{
	if(mobj.MapObjType!=MAP_OBJECT_ITEM && mobj.MapObjType!=MAP_OBJECT_SCENERY) SCRIPT_ERROR_RX("Map object is not item or scenery.",new CScriptString(""));
	return new CScriptString(mobj.RunTime.PicMapName);
}

void FOMapper::SScriptFunc::MapperObject_set_PicMap(MapObject& mobj, CScriptString* str)
{
	if(mobj.MapObjType!=MAP_OBJECT_ITEM && mobj.MapObjType!=MAP_OBJECT_SCENERY) SCRIPT_ERROR_R("Map object is not item or scenery.");
	StringCopy(mobj.RunTime.PicMapName,str->c_str());
	mobj.MItem.PicMapHash=Str::GetHash(mobj.RunTime.PicMapName);
}

CScriptString* FOMapper::SScriptFunc::MapperObject_get_PicInv(MapObject& mobj)
{
	if(mobj.MapObjType!=MAP_OBJECT_ITEM && mobj.MapObjType!=MAP_OBJECT_SCENERY) SCRIPT_ERROR_RX("Map object is not item or scenery.",new CScriptString(""));
	return new CScriptString(mobj.RunTime.PicInvName);
}

void FOMapper::SScriptFunc::MapperObject_set_PicInv(MapObject& mobj, CScriptString* str)
{
	if(mobj.MapObjType!=MAP_OBJECT_ITEM && mobj.MapObjType!=MAP_OBJECT_SCENERY) SCRIPT_ERROR_R("Map object is not item or scenery.");
	StringCopy(mobj.RunTime.PicInvName,str->c_str());
	mobj.MItem.PicInvHash=Str::GetHash(mobj.RunTime.PicInvName);
}

void FOMapper::SScriptFunc::MapperObject_Update(MapObject& mobj)
{
	if(mobj.RunTime.FromMap && mobj.RunTime.FromMap==Self->CurProtoMap) Self->UpdateMapObject(&mobj);
}

MapObject* FOMapper::SScriptFunc::MapperObject_AddChild(MapObject& mobj, WORD pid)
{
	if(mobj.MapObjType!=MAP_OBJECT_CRITTER && mobj.MapObjType!=MAP_OBJECT_ITEM) SCRIPT_ERROR_R0("Invalid map object type.");
	if(mobj.MapObjType==MAP_OBJECT_ITEM && mobj.MItem.InContainer) SCRIPT_ERROR_R0("Object already in container.");
	ProtoItem* proto_item=ItemMngr.GetProtoItem(pid);
	if(!proto_item || !proto_item->IsContainer()) SCRIPT_ERROR_R0("Object is not container item.");
	ProtoItem* proto_item_=ItemMngr.GetProtoItem(pid);
	if(!proto_item_ || !proto_item_->IsItem()) SCRIPT_ERROR_R0("Added child is not item.");

	MapObject* mobj_=new MapObject();
	if(!mobj_) return NULL;
	mobj_->RunTime.FromMap=mobj.RunTime.FromMap;
	mobj_->MapObjType=MAP_OBJECT_ITEM;
	mobj_->ProtoId=pid;
	mobj_->MapX=mobj.MapX;
	mobj_->MapY=mobj.MapY;
	mobj_->MItem.InContainer;
	return mobj_;
}

DWORD FOMapper::SScriptFunc::MapperObject_GetChilds(MapObject& mobj, asIScriptArray* objects)
{
	if(!mobj.RunTime.FromMap) return 0;
	if(mobj.MapObjType!=MAP_OBJECT_ITEM) return 0;
	MapObjectPtrVec objects_;
	for(MapObjectPtrVecIt it=mobj.RunTime.FromMap->MObjects.begin();it!=mobj.RunTime.FromMap->MObjects.end();++it)
	{
		MapObject* mobj_=*it;
		if(mobj_->MapObjType==MAP_OBJECT_ITEM && mobj_->MapX==mobj.MapX && mobj_->MapY==mobj.MapY && mobj_->MItem.InContainer)
			objects_.push_back(mobj_);
	}
	if(objects) Script::AppendVectorToArrayRef(objects_,objects);
	return objects_.size();
}

void FOMapper::SScriptFunc::MapperObject_MoveToHex(MapObject& mobj, WORD hx, WORD hy)
{
	ProtoMap* pmap=mobj.RunTime.FromMap;
	if(!pmap) return;
	if(mobj.MapObjType==MAP_OBJECT_ITEM && mobj.MItem.InContainer) return;

	hx=CLAMP(hx,0,pmap->Header.MaxHexX-1);
	hy=CLAMP(hy,0,pmap->Header.MaxHexY-1);
	Self->MoveMapObject(&mobj,hx,hy);
}

void FOMapper::SScriptFunc::MapperObject_MoveToHexOffset(MapObject& mobj, int x, int y)
{
	ProtoMap* pmap=mobj.RunTime.FromMap;
	if(!pmap) return;
	if(mobj.MapObjType==MAP_OBJECT_ITEM && mobj.MItem.InContainer) return;

	int hx=(int)mobj.MapX+x;
	int hy=(int)mobj.MapY+y;
	hx=CLAMP(hx,0,pmap->Header.MaxHexX-1);
	hy=CLAMP(hy,0,pmap->Header.MaxHexY-1);
	Self->MoveMapObject(&mobj,hx,hy);
}

void FOMapper::SScriptFunc::MapperObject_MoveToDir(MapObject& mobj, BYTE dir)
{
	ProtoMap* pmap=mobj.RunTime.FromMap;
	if(!pmap) return;
	if(mobj.MapObjType==MAP_OBJECT_ITEM && mobj.MItem.InContainer) return;

	int hx=mobj.MapX;
	int hy=mobj.MapY;
	MoveHexByDirUnsafe(hx,hy,dir);
	hx=CLAMP(hx,0,pmap->Header.MaxHexX-1);
	hy=CLAMP(hy,0,pmap->Header.MaxHexY-1);
	Self->MoveMapObject(&mobj,hx,hy);
}

MapObject* FOMapper::SScriptFunc::MapperMap_AddObject(ProtoMap& pmap, WORD hx, WORD hy, int mobj_type, WORD pid)
{
	if(mobj_type==MAP_OBJECT_CRITTER)
	{
		for(MapObjectPtrVecIt it=pmap.MObjects.begin();it!=pmap.MObjects.end();++it)
		{
			MapObject* mobj=*it;
			if(mobj->MapObjType==MAP_OBJECT_CRITTER && mobj->MCritter.Cond!=COND_DEAD &&
				mobj->MapX==hx && mobj->MapY==hy) SCRIPT_ERROR_R0("Critter already present on this hex.");
		}

		CritData* pnpc=CrMngr.GetProto(pid);
		if(!pnpc) SCRIPT_ERROR_R0("Unknown critter prototype.");
	}
	else if(mobj_type==MAP_OBJECT_ITEM || mobj_type==MAP_OBJECT_SCENERY)
	{
		ProtoItem* proto=ItemMngr.GetProtoItem(pid);
		if(!proto) SCRIPT_ERROR_R0("Invalid item/scenery prototype.");
	}
	else
	{
		SCRIPT_ERROR_R0("Invalid map object type.");
	}

	MapObject* mobj=new MapObject();
	if(!mobj) return NULL;
	mobj->RunTime.FromMap=&pmap;
	mobj->MapObjType=mobj_type;
	mobj->ProtoId=pid;
	mobj->MapX=hx;
	mobj->MapY=hy;
	if(mobj_type==MAP_OBJECT_CRITTER)
	{
		mobj->MCritter.Cond=COND_LIFE;
		mobj->MCritter.CondExt=COND_LIFE_NONE;
	}

	if(Self->CurProtoMap==&pmap)
	{
		MapObject* mobj_=Self->ParseMapObj(mobj);
		SAFEREL(mobj);
		mobj=mobj_;
	}
	else
	{
		pmap.MObjects.push_back(mobj);
	}
	return mobj;
}

MapObject* FOMapper::SScriptFunc::MapperMap_GetObject(ProtoMap& pmap, WORD hx, WORD hy, int mobj_type, WORD pid, DWORD skip)
{
	return Self->FindMapObject(pmap,hx,hy,mobj_type,pid,skip);
}

DWORD FOMapper::SScriptFunc::MapperMap_GetObjects(ProtoMap& pmap, WORD hx, WORD hy, DWORD radius, int mobj_type, WORD pid, asIScriptArray* objects)
{
	MapObjectPtrVec objects_;
	Self->FindMapObjects(pmap,hx,hy,radius,mobj_type,pid,objects_);
	if(objects) Script::AppendVectorToArrayRef(objects_,objects);
	return objects_.size();
}

void FOMapper::SScriptFunc::MapperMap_UpdateObjects(ProtoMap& pmap)
{
	if(Self->CurProtoMap==&pmap)
	{
		for(MapObjectPtrVecIt it=pmap.MObjects.begin(),end=pmap.MObjects.end();it!=end;++it)
			Self->UpdateMapObject(*it);
	}
}

void FOMapper::SScriptFunc::MapperMap_Resize(ProtoMap& pmap, WORD width, WORD height)
{
	if(Self->CurProtoMap==&pmap) Self->HexMngr.UnloadMap();

	int maxhx=CLAMP(width,MAXHEX_MIN,MAXHEX_MAX);
	int maxhy=CLAMP(height,MAXHEX_MIN,MAXHEX_MAX);;
	int old_maxhx=pmap.Header.MaxHexX;
	int old_maxhy=pmap.Header.MaxHexY;
	maxhx=CLAMP(maxhx,MAXHEX_MIN,MAXHEX_MAX);
	maxhy=CLAMP(maxhy,MAXHEX_MIN,MAXHEX_MAX);
	if(pmap.Header.CenterX>=maxhx) pmap.Header.CenterX=maxhx-1;
	if(pmap.Header.CenterY>=maxhy) pmap.Header.CenterY=maxhy-1;
	pmap.Header.MaxHexX=maxhx;
	pmap.Header.MaxHexY=maxhy;

	// Delete truncated objects
	for(MapObjectPtrVecIt it=pmap.MObjects.begin();it!=pmap.MObjects.end();)
	{
		MapObject* mobj=*it;
		if(mobj->MapX>=maxhx || mobj->MapY>=maxhy)
		{
			SAFEREL(mobj);
			it=pmap.MObjects.erase(it);
		}
		else ++it;
	}

	// Resize tiles
	DWORD* old_tiles=pmap.Tiles;
	pmap.Tiles=new DWORD[pmap.GetTilesSize()/sizeof(DWORD)];
	ZeroMemory(pmap.Tiles,pmap.GetTilesSize());
	for(int tx=0,ex=min(old_maxhx/2,pmap.Header.MaxHexX/2);tx<ex;tx++)
	{
		for(int ty=0,ey=min(old_maxhy/2,pmap.Header.MaxHexY/2);ty<ey;ty++)
		{
			pmap.Tiles[ty*(pmap.Header.MaxHexX/2)*2+tx*2]=old_tiles[ty*(old_maxhx/2)*2+tx*2];
			pmap.Tiles[ty*(pmap.Header.MaxHexX/2)*2+tx*2+1]=old_tiles[ty*(old_maxhx/2)*2+tx*2+1];
		}
	}
	delete[] old_tiles;

	// Update visibility
	if(Self->CurProtoMap==&pmap)
	{
		Self->HexMngr.SetProtoMap(pmap);
		Self->HexMngr.FindSetCenter(pmap.Header.CenterX,pmap.Header.CenterY);
	}
}

DWORD FOMapper::SScriptFunc::MapperMap_GetTileHash(ProtoMap& pmap, WORD tx, WORD ty, bool roof)
{
	if(tx>=pmap.Header.MaxHexX/2) SCRIPT_ERROR_R0("Invalid tile x arg.");
	if(ty>=pmap.Header.MaxHexY/2) SCRIPT_ERROR_R0("Invalid tile y arg.");
	if(roof) return pmap.GetRoof(tx,ty);
	return pmap.GetTile(tx,ty);
}

void FOMapper::SScriptFunc::MapperMap_SetTileHash(ProtoMap& pmap, WORD tx, WORD ty, bool roof, DWORD pic_hash)
{
	if(tx>=pmap.Header.MaxHexX/2) SCRIPT_ERROR_R("Invalid tile x arg.");
	if(ty>=pmap.Header.MaxHexY/2) SCRIPT_ERROR_R("Invalid tile y arg.");
	if(Self->CurProtoMap==&pmap) Self->HexMngr.SetTile(tx*2,ty*2,pic_hash,roof);
	else
	{
		if(roof) pmap.SetRoof(tx,ty,pic_hash);
		else pmap.SetTile(tx,ty,pic_hash);
	}
}

CScriptString* FOMapper::SScriptFunc::MapperMap_GetTileName(ProtoMap& pmap, WORD tx, WORD ty, bool roof)
{
	if(tx>=pmap.Header.MaxHexX/2) SCRIPT_ERROR_RX("Invalid tile x arg.",new CScriptString(""));
	if(ty>=pmap.Header.MaxHexY/2) SCRIPT_ERROR_RX("Invalid tile y arg.",new CScriptString(""));
	DWORD hash=(roof?pmap.GetRoof(tx,ty):pmap.GetTile(tx,ty));
	const char* name=ResMngr.GetName(hash);
	return new CScriptString(name?name:"");
}

void FOMapper::SScriptFunc::MapperMap_SetTileName(ProtoMap& pmap, WORD tx, WORD ty, bool roof, CScriptString* pic_name)
{
	if(tx>=pmap.Header.MaxHexX/2) SCRIPT_ERROR_R("Invalid tile x arg.");
	if(ty>=pmap.Header.MaxHexY/2) SCRIPT_ERROR_R("Invalid tile y arg.");
	DWORD hash=Str::GetHash(pic_name && pic_name->length()?pic_name->c_str():NULL);
	if(Self->CurProtoMap==&pmap) Self->HexMngr.SetTile(tx*2,ty*2,hash,roof);
	else
	{
		if(roof) pmap.SetRoof(tx,ty,hash);
		else pmap.SetTile(tx,ty,hash);
	}
}

CScriptString* FOMapper::SScriptFunc::MapperMap_GetTerrainName(ProtoMap& pmap, WORD tx, WORD ty)
{
	if(tx>=pmap.Header.MaxHexX/2) SCRIPT_ERROR_RX("Invalid tile x arg.",new CScriptString(""));
	if(ty>=pmap.Header.MaxHexY/2) SCRIPT_ERROR_RX("Invalid tile y arg.",new CScriptString(""));
	if(pmap.GetRoof(tx,ty)!=0xAAAAAAAA) return new CScriptString("");
	const char* name=ResMngr.GetName(pmap.GetTile(tx,ty));
	return new CScriptString(name?name:"");
}

void FOMapper::SScriptFunc::MapperMap_SetTerrainName(ProtoMap& pmap, WORD tx, WORD ty, CScriptString* terrain_name)
{
	if(tx>=pmap.Header.MaxHexX/2) SCRIPT_ERROR_R("Invalid tile x arg.");
	if(ty>=pmap.Header.MaxHexY/2) SCRIPT_ERROR_R("Invalid tile y arg.");
	DWORD hash=Str::GetHash(terrain_name && terrain_name->length()?terrain_name->c_str():NULL);
	if(Self->CurProtoMap==&pmap) Self->HexMngr.SetTerrain(tx*2,ty*2,hash);
	else
	{
		pmap.SetRoof(tx,ty,hash?0xAAAAAAAA:0);
		pmap.SetTile(tx,ty,hash);
	}
}

DWORD FOMapper::SScriptFunc::MapperMap_GetDayTime(ProtoMap& pmap, DWORD day_part)
{
	if(day_part>=4) SCRIPT_ERROR_R0("Invalid day part arg.");
	return pmap.Header.DayTime[day_part];
}

void FOMapper::SScriptFunc::MapperMap_SetDayTime(ProtoMap& pmap, DWORD day_part, DWORD time)
{
	if(day_part>=4) SCRIPT_ERROR_R("Invalid day part arg.");
	if(time>=1440) SCRIPT_ERROR_R("Invalid time arg.");

	pmap.Header.DayTime[day_part]=time;
	if(pmap.Header.DayTime[1]<pmap.Header.DayTime[0]) pmap.Header.DayTime[1]=pmap.Header.DayTime[0];
	if(pmap.Header.DayTime[2]<pmap.Header.DayTime[1]) pmap.Header.DayTime[2]=pmap.Header.DayTime[1];
	if(pmap.Header.DayTime[3]<pmap.Header.DayTime[2]) pmap.Header.DayTime[3]=pmap.Header.DayTime[2];

	// Update visibility
	if(Self->CurProtoMap==&pmap)
	{
		int* dt=Self->HexMngr.GetMapDayTime();
		for(int i=0;i<4;i++) dt[i]=pmap.Header.DayTime[i];
		Self->HexMngr.RefreshMap();
	}
}

void FOMapper::SScriptFunc::MapperMap_GetDayColor(ProtoMap& pmap, DWORD day_part, BYTE& r, BYTE& g, BYTE& b)
{
	if(day_part>=4) SCRIPT_ERROR_R("Invalid day part arg.");
	r=pmap.Header.DayColor[0+day_part];
	g=pmap.Header.DayColor[4+day_part];
	b=pmap.Header.DayColor[8+day_part];
}

void FOMapper::SScriptFunc::MapperMap_SetDayColor(ProtoMap& pmap, DWORD day_part, BYTE r, BYTE g, BYTE b)
{
	if(day_part>=4) SCRIPT_ERROR_R("Invalid day part arg.");

	pmap.Header.DayColor[0+day_part]=r;
	pmap.Header.DayColor[4+day_part]=g;
	pmap.Header.DayColor[8+day_part]=b;

	// Update visibility
	if(Self->CurProtoMap==&pmap)
	{
		BYTE* dc=Self->HexMngr.GetMapDayColor();
		for(int i=0;i<12;i++) dc[i]=pmap.Header.DayColor[i];
		Self->HexMngr.RefreshMap();
	}
}

void FOMapper::SScriptFunc::Global_SetDefaultCritterParam(DWORD index, int param)
{
	if(index>=MAPOBJ_CRITTER_PARAMS) SCRIPT_ERROR_R("Invalid index arg.");
	Self->DefaultCritterParam[index]=param;
}

DWORD FOMapper::SScriptFunc::Global_GetFastPrototypes(asIScriptArray* pids)
{
	WordVec pids_;
	for(size_t i=0,j=Self->FastItemProto.size();i<j;i++) pids_.push_back(Self->FastItemProto[i].GetPid());
	if(pids) Script::AppendVectorToArray(pids_,pids);
	return pids_.size();
}

void FOMapper::SScriptFunc::Global_SetFastPrototypes(asIScriptArray* pids)
{
	Self->HexMngr.ClearFastPids();
	Self->FastItemProto.clear();
	if(pids)
	{
		WordVec pids_;
		Script::AssignScriptArrayInVector(pids_,pids);
		for(size_t i=0,j=pids_.size();i<j;i++) Self->AddFastProto(pids_[i]);
	}
}

ProtoMap* FOMapper::SScriptFunc::Global_LoadMap(CScriptString& file_name, int path_type)
{
	ProtoMap* pmap=new ProtoMap();
	if(!pmap->Init(0xFFFF,file_name.c_str(),path_type)) return NULL;
	Self->LoadedProtoMaps.push_back(pmap);
	return pmap;
}

void FOMapper::SScriptFunc::Global_UnloadMap(ProtoMap* pmap)
{
	if(!pmap) SCRIPT_ERROR_R("Proto map arg nullptr.");
	ProtoMapPtrVec::iterator it=std::find(Self->LoadedProtoMaps.begin(),Self->LoadedProtoMaps.end(),pmap);
	if(it!=Self->LoadedProtoMaps.end()) Self->LoadedProtoMaps.erase(it);
	if(pmap==Self->CurProtoMap)
	{
		Self->HexMngr.UnloadMap();
		Self->SelectedObj.clear();
		SAFEREL(Self->CurProtoMap);
	}
	pmap->Clear();
	pmap->Release();
}

bool FOMapper::SScriptFunc::Global_SaveMap(ProtoMap* pmap, CScriptString& file_name, int path_type, bool text, bool pack)
{
	if(!pmap) SCRIPT_ERROR_R0("Proto map arg nullptr.");
	return pmap->Save(file_name.c_str(),path_type,text,pack);
}

bool FOMapper::SScriptFunc::Global_ShowMap(ProtoMap* pmap)
{
	if(!pmap) SCRIPT_ERROR_R0("Proto map arg nullptr.");
	if(Self->CurProtoMap==pmap) return true; // Already

	Self->SelectClear();
	if(!Self->HexMngr.SetProtoMap(*pmap)) return false;
	Self->HexMngr.FindSetCenter(pmap->Header.CenterX,pmap->Header.CenterY);
	Self->CurProtoMap=pmap;
	return true;
}

int FOMapper::SScriptFunc::Global_GetLoadedMaps(asIScriptArray* maps)
{
	int index=-1;
	for(int i=0,j=(int)Self->LoadedProtoMaps.size();i<j;i++)
	{
		ProtoMap* pmap=Self->LoadedProtoMaps[i];
		if(pmap==Self->CurProtoMap) index=i;
	}
	if(maps) Script::AppendVectorToArrayRef(Self->LoadedProtoMaps,maps);
	return index;
}

DWORD FOMapper::SScriptFunc::Global_GetMapFileNames(CScriptString* dir, asIScriptArray* names)
{
	string dir_=FileManager::GetFullPath(NULL,PT_MAPS);
	if(dir) dir_=dir->c_std_str();

	WIN32_FIND_DATA fdata;
	HANDLE h=FindFirstFile((dir_+"*.*").c_str(),&fdata);
	if(h==INVALID_HANDLE_VALUE) return 0;

	DWORD count=0;
	while(true)
	{
		if(ProtoMap::IsMapFile(Str::Format("%s%s",dir_.c_str(),fdata.cFileName)))
		{
			WriteLog("map<%s>\n",fdata.cFileName);

			count++;
			if(names)
			{
				int len=names->GetElementCount();
				names->Resize(names->GetElementCount()+1);
				CScriptString** ptr=(CScriptString**)names->GetElementPointer(len);
				*ptr=new CScriptString(fdata.cFileName);
			}
		}

		if(!FindNextFile(h,&fdata)) break;
	}

	return count;
}

void FOMapper::SScriptFunc::Global_DeleteObject(MapObject* mobj)
{
	if(mobj) Self->DeleteMapObject(mobj);
}

void FOMapper::SScriptFunc::Global_DeleteObjects(asIScriptArray& objects)
{
	for(int i=0,j=objects.GetElementCount();i<j;i++)
	{
		MapObject* mobj=*(MapObject**)objects.GetElementPointer(i);
		if(mobj) Self->DeleteMapObject(mobj);
	}
}

void FOMapper::SScriptFunc::Global_SelectObject(MapObject* mobj, bool set)
{
	if(mobj)
	{
		if(set) Self->SelectAdd(mobj);
		else Self->SelectErase(mobj);
	}
}

void FOMapper::SScriptFunc::Global_SelectObjects(asIScriptArray& objects, bool set)
{
	for(int i=0,j=objects.GetElementCount();i<j;i++)
	{
		MapObject* mobj=*(MapObject**)objects.GetElementPointer(i);
		if(mobj)
		{
			if(set) Self->SelectAdd(mobj);
			else Self->SelectErase(mobj);
		}
	}
}

MapObject* FOMapper::SScriptFunc::Global_GetSelectedObject()
{
	return Self->SelectedObj.size()?Self->SelectedObj[0].MapObj:NULL;
}

DWORD FOMapper::SScriptFunc::Global_GetSelectedObjects(asIScriptArray* objects)
{
	MapObjectPtrVec objects_;
	objects_.reserve(Self->SelectedObj.size());
	for(size_t i=0,j=Self->SelectedObj.size();i<j;i++) objects_.push_back(Self->SelectedObj[i].MapObj);
	if(objects) Script::AppendVectorToArrayRef(objects_,objects);
	return objects_.size();
}

CScriptString* FOMapper::SScriptFunc::Global_GetLastError()
{
	return new CScriptString(ScriptLastError);
}

ProtoItem* FOMapper::SScriptFunc::Global_GetProtoItem(WORD proto_id)
{
	ProtoItem* proto_item=ItemMngr.GetProtoItem(proto_id);
	//if(!proto_item) SCRIPT_ERROR_R0("Proto item not found.");
	return proto_item;
}

bool FOMapper::SScriptFunc::Global_LoadDat(CScriptString& dat_name)
{
	if(FileManager::LoadDat(dat_name.c_str()))
	{
		// Reload resource manager
		if(Self->IsMapperStarted)
		{
			ResMngr.Refresh(NULL);
			Self->RefreshTiles();
		}
		return true;
	}
	return false;
}

void FOMapper::SScriptFunc::Global_MoveScreen(WORD hx, WORD hy, DWORD speed)
{
	if(hx>=Self->HexMngr.GetMaxHexX() || hy>=Self->HexMngr.GetMaxHexY()) SCRIPT_ERROR_R("Invalid hex args.");
	if(!Self->HexMngr.IsMapLoaded()) SCRIPT_ERROR_R("Map is not loaded.");
	if(!speed) Self->HexMngr.FindSetCenter(hx,hy);
	else Self->HexMngr.ScrollToHex(hx,hy,double(speed)/1000.0,false);
}

void FOMapper::SScriptFunc::Global_MoveHexByDir(WORD& hx, WORD& hy, BYTE dir, DWORD steps)
{
	if(!Self->HexMngr.IsMapLoaded()) SCRIPT_ERROR_R("Map not loaded.");
	if(dir>5) SCRIPT_ERROR_R("Invalid dir arg.");
	if(!steps) SCRIPT_ERROR_R("Steps arg is zero.");
	int hx_=hx,hy_=hy;
	if(steps>1)
	{
		for(int i=0;i<steps;i++) MoveHexByDirUnsafe(hx_,hy_,dir);
	}
	else
	{
		MoveHexByDirUnsafe(hx_,hy_,dir);
	}
	if(hx_<0) hx_=0;
	if(hy_<0) hy_=0;
	hx=hx_;
	hy=hy_;
}

CScriptString* FOMapper::SScriptFunc::Global_GetIfaceIniStr(CScriptString& key)
{
	char* big_buf=Str::GetBigBuf();
	if(Self->IfaceIni.GetStr(key.c_str(),"",big_buf)) return new CScriptString(big_buf);
	return new CScriptString("");
}

void FOMapper::SScriptFunc::Global_Log(CScriptString& text)
{
	//Self->AddMessFormat(text.buffer.c_str());
	Script::Log(text.c_str());
}

bool FOMapper::SScriptFunc::Global_StrToInt(CScriptString& text, int& result)
{
	if(!text.length()) return false;
	return Str::StrToInt(text.c_str(),result);
}

void FOMapper::SScriptFunc::Global_Message(CScriptString& msg)
{
	Self->AddMess(msg.c_str());
}

void FOMapper::SScriptFunc::Global_MessageMsg(int text_msg, DWORD str_num)
{
	if(text_msg>=TEXTMSG_COUNT) SCRIPT_ERROR_R("Invalid text msg arg.");
	Self->AddMess(Self->CurLang.Msg[text_msg].GetStr(str_num));
}

void FOMapper::SScriptFunc::Global_MapMessage(CScriptString& text, WORD hx, WORD hy, DWORD ms, DWORD color, bool fade, int ox, int oy)
{
	FOMapper::MapText t;
	t.HexX=hx;
	t.HexY=hy;
	t.Color=(color?color:COLOR_TEXT);
	t.Fade=fade;
	t.StartTick=Timer::FastTick();
	t.Tick=ms;
	t.Text=text.c_std_str();
	t.Rect=Self->HexMngr.GetRectForText(hx,hy);
	t.EndRect=INTRECT(t.Rect,ox,oy);
	MapTextVecIt it=std::find(Self->GameMapTexts.begin(),Self->GameMapTexts.end(),t);
	if(it!=Self->GameMapTexts.end()) Self->GameMapTexts.erase(it);
	Self->GameMapTexts.push_back(t);
}

CScriptString* FOMapper::SScriptFunc::Global_GetMsgStr(int text_msg, DWORD str_num)
{
	if(text_msg>=TEXTMSG_COUNT) SCRIPT_ERROR_R0("Invalid text msg arg.");
	CScriptString* str=new CScriptString(Self->CurLang.Msg[text_msg].GetStr(str_num));
	return str;
}

CScriptString* FOMapper::SScriptFunc::Global_GetMsgStrSkip(int text_msg, DWORD str_num, DWORD skip_count)
{
	if(text_msg>=TEXTMSG_COUNT) SCRIPT_ERROR_R0("Invalid text msg arg.");
	CScriptString* str=new CScriptString(Self->CurLang.Msg[text_msg].GetStr(str_num,skip_count));
	return str;
}

DWORD FOMapper::SScriptFunc::Global_GetMsgStrNumUpper(int text_msg, DWORD str_num)
{
	if(text_msg>=TEXTMSG_COUNT) SCRIPT_ERROR_R0("Invalid text msg arg.");
	return Self->CurLang.Msg[text_msg].GetStrNumUpper(str_num);
}

DWORD FOMapper::SScriptFunc::Global_GetMsgStrNumLower(int text_msg, DWORD str_num)
{
	if(text_msg>=TEXTMSG_COUNT) SCRIPT_ERROR_R0("Invalid text msg arg.");
	return Self->CurLang.Msg[text_msg].GetStrNumLower(str_num);
}

DWORD FOMapper::SScriptFunc::Global_GetMsgStrCount(int text_msg, DWORD str_num)
{
	if(text_msg>=TEXTMSG_COUNT) SCRIPT_ERROR_R0("Invalid text msg arg.");
	return Self->CurLang.Msg[text_msg].Count(str_num);
}

bool FOMapper::SScriptFunc::Global_IsMsgStr(int text_msg, DWORD str_num)
{
	if(text_msg>=TEXTMSG_COUNT) SCRIPT_ERROR_R0("Invalid text msg arg.");
	return Self->CurLang.Msg[text_msg].Count(str_num)>0;
}

CScriptString* FOMapper::SScriptFunc::Global_ReplaceTextStr(CScriptString& text, CScriptString& replace, CScriptString& str)
{
	size_t pos=text.c_std_str().find(replace.c_std_str(),0);
	if(pos==std::string::npos) return new CScriptString(text);
	string str_=text.c_std_str();
	return new CScriptString(str_.replace(pos,replace.length(),str_));
}

CScriptString* FOMapper::SScriptFunc::Global_ReplaceTextInt(CScriptString& text, CScriptString& replace, int i)
{
	size_t pos=text.c_std_str().find(replace.c_std_str(),0);
	if(pos==std::string::npos) return new CScriptString(text);
	char val[32];
	sprintf(val,"%d",i);
	string str_=text.c_std_str();
	return new CScriptString(str_.replace(pos,replace.length(),val));
}

DWORD FOMapper::SScriptFunc::Global_GetDistantion(WORD hex_x1, WORD hex_y1, WORD hex_x2, WORD hex_y2)
{
	return DistGame(hex_x1,hex_y1,hex_x2,hex_y2);
}

BYTE FOMapper::SScriptFunc::Global_GetDirection(WORD from_x, WORD from_y, WORD to_x, WORD to_y)
{
	return GetFarDir(from_x,from_y,to_x,to_y);
}

BYTE FOMapper::SScriptFunc::Global_GetOffsetDir(WORD hx, WORD hy, WORD tx, WORD ty, float offset)
{
	float nx=3.0f*(float(tx)-float(hx));
	float ny=SQRT3T2_FLOAT*(float(ty)-float(hy))-SQRT3_FLOAT*(float(tx%2)-float(hx%2));
	float dir=180.0f+RAD2DEG*atan2(ny,nx);
	dir+=offset;
	if(dir>360.0f) dir-=360.0f;
	else if(dir<0.0f) dir+=360.0f;
	if(dir>=60.0f  && dir<120.0f) return 5;
	if(dir>=120.0f && dir<180.0f) return 4;
	if(dir>=180.0f && dir<240.0f) return 3;
	if(dir>=240.0f && dir<300.0f) return 2;
	if(dir>=300.0f && dir<360.0f) return 1;
	return 0;
}

void FOMapper::SScriptFunc::Global_GetHexInPath(WORD from_hx, WORD from_hy, WORD& to_hx, WORD& to_hy, float angle, DWORD dist)
{
	WordPair pre_block,block;
	Self->HexMngr.TraceBullet(from_hx,from_hy,to_hx,to_hy,dist,angle,NULL,false,NULL,0,&block,&pre_block,NULL,true);
	to_hx=pre_block.first;
	to_hy=pre_block.second;
}

DWORD FOMapper::SScriptFunc::Global_GetPathLength(WORD from_hx, WORD from_hy, WORD to_hx, WORD to_hy, DWORD cut)
{
	if(from_hx>=Self->HexMngr.GetMaxHexX() || from_hy>=Self->HexMngr.GetMaxHexY()) SCRIPT_ERROR_R0("Invalid from hexes args.");
	if(to_hx>=Self->HexMngr.GetMaxHexX() || to_hy>=Self->HexMngr.GetMaxHexY()) SCRIPT_ERROR_R0("Invalid to hexes args.");

	if(cut && Self->HexMngr.CutPath(from_hx,from_hy,to_hx,to_hy,cut)!=FP_OK) return 0;
	ByteVec steps;
	Self->HexMngr.FindStep(from_hx,from_hy,to_hx,to_hy,steps);
	return steps.size();
}

bool FOMapper::SScriptFunc::Global_GetHexPos(WORD hx, WORD hy, int& x, int& y)
{
	x=y=0;
	if(Self->HexMngr.IsMapLoaded() && hx<Self->HexMngr.GetMaxHexX() && hy<Self->HexMngr.GetMaxHexY())
	{
		Self->HexMngr.GetHexCurrentPosition(hx,hy,x,y);
		x+=CmnScrOx;
		y+=CmnScrOy;
		x/=SpritesZoom;
		y/=SpritesZoom;
		return true;
	}
	return false;
}

bool FOMapper::SScriptFunc::Global_GetMonitorHex(int x, int y, WORD& hx, WORD& hy)
{
	WORD hx_=0,hy_=0;
	if(Self->GetMouseHex(hx_,hy_))
	{
		hx=hx_;
		hy=hy_;
		return true;
	}
	return false;
}

void FOMapper::SScriptFunc::Global_GetMousePosition(int& x, int& y)
{
	x=Self->CurX;
	y=Self->CurY;
}

DWORD FOMapper::SScriptFunc::Global_GetAngelScriptProperty(int property)
{
	asIScriptEngine* engine=Script::GetEngine();
	if(!engine) SCRIPT_ERROR_R0("Can't get engine.");
	return engine->GetEngineProperty((asEEngineProp)property);
}

bool FOMapper::SScriptFunc::Global_SetAngelScriptProperty(int property, DWORD value)
{
	asIScriptEngine* engine=Script::GetEngine();
	if(!engine) SCRIPT_ERROR_R0("Can't get engine.");
	int result=engine->SetEngineProperty((asEEngineProp)property,value);
	if(result<0) SCRIPT_ERROR_R0("Invalid data. Property not setted.");
	return true;
}

DWORD FOMapper::SScriptFunc::Global_GetStrHash(CScriptString* str)
{
	if(str) return Str::GetHash(str->c_str());
	return 0;
}

DWORD FOMapper::SScriptFunc::Global_LoadSprite(CScriptString& spr_name, int path_index)
{
	if(path_index>=PATH_LIST_COUNT) SCRIPT_ERROR_R0("Invalid path index arg.");
	return Self->AnimLoad(spr_name.c_str(),path_index,RES_SCRIPT);
}

DWORD FOMapper::SScriptFunc::Global_LoadSpriteHash(DWORD name_hash, BYTE dir)
{
	return Self->AnimLoad(name_hash,dir,RES_SCRIPT);
}

int FOMapper::SScriptFunc::Global_GetSpriteWidth(DWORD spr_id, int spr_index)
{
	AnyFrames* anim=Self->AnimGetFrames(spr_id);
	if(!anim || spr_index>=anim->GetCnt()) return 0;
	SpriteInfo* si=Self->SprMngr.GetSpriteInfo(spr_index<0?anim->GetCurSprId():anim->GetSprId(spr_index));
	if(!si) return 0;
	return si->Width;
}

int FOMapper::SScriptFunc::Global_GetSpriteHeight(DWORD spr_id, int spr_index)
{
	AnyFrames* anim=Self->AnimGetFrames(spr_id);
	if(!anim || spr_index>=anim->GetCnt()) return 0;
	SpriteInfo* si=Self->SprMngr.GetSpriteInfo(spr_index<0?anim->GetCurSprId():anim->GetSprId(spr_index));
	if(!si) return 0;
	return si->Height;
}

DWORD FOMapper::SScriptFunc::Global_GetSpriteCount(DWORD spr_id)
{
	AnyFrames* anim=Self->AnimGetFrames(spr_id);
	return anim?anim->CntFrm:0;
}

void FOMapper::SScriptFunc::Global_DrawSprite(DWORD spr_id, int spr_index, int x, int y, DWORD color)
{
	if(!SpritesCanDraw || !spr_id) return;
	AnyFrames* anim=Self->AnimGetFrames(spr_id);
	if(!anim || spr_index>=anim->GetCnt()) return;
	Self->SprMngr.DrawSprite(spr_index<0?anim->GetCurSprId():anim->GetSprId(spr_index),x,y,color);
}

void FOMapper::SScriptFunc::Global_DrawSpriteSize(DWORD spr_id, int spr_index, int x, int y, int w, int h, bool scratch, bool center, DWORD color)
{
	if(!SpritesCanDraw || !spr_id) return;
	AnyFrames* anim=Self->AnimGetFrames(spr_id);
	if(!anim || spr_index>=anim->GetCnt()) return;
	Self->SprMngr.DrawSpriteSize(spr_index<0?anim->GetCurSprId():anim->GetSprId(spr_index),x,y,w,h,scratch,true,color);
}

void FOMapper::SScriptFunc::Global_DrawText(CScriptString& text, int x, int y, int w, int h, DWORD color, int font, int flags)
{
	if(!SpritesCanDraw) return;
	Self->SprMngr.DrawStr(INTRECT(x,y,x+w,y+h),text.c_str(),flags,color,font);
}

void FOMapper::SScriptFunc::Global_DrawPrimitive(int primitive_type, asIScriptArray& data)
{
	if(!SpritesCanDraw) return;

	D3DPRIMITIVETYPE prim;
	switch(primitive_type)
	{
	case 0: prim=D3DPT_POINTLIST; break;
	case 1: prim=D3DPT_LINELIST; break;
	case 2: prim=D3DPT_LINESTRIP; break;
	case 3: prim=D3DPT_TRIANGLELIST; break;
	case 4: prim=D3DPT_TRIANGLESTRIP; break;
	case 5: prim=D3DPT_TRIANGLEFAN; break;
	default: return;
	}

	int size=data.GetElementCount()/3;
	PointVec points;
	points.resize(size);

	for(int i=0;i<size;i++)
	{
		PrepPoint& pp=points[i];
		pp.PointX=*(int*)data.GetElementPointer(i*3);
		pp.PointY=*(int*)data.GetElementPointer(i*3+1);
		pp.PointColor=*(int*)data.GetElementPointer(i*3+2);
		//pp.PointOffsX=NULL;
		//pp.PointOffsY=NULL;
	}

	Self->SprMngr.DrawPoints(points,prim);
}

void FOMapper::SScriptFunc::Global_DrawMapSprite(WORD hx, WORD hy, WORD proto_id, DWORD spr_id, int spr_index, int ox, int oy)
{
	if(!Self->HexMngr.SpritesCanDrawMap) return;
	if(!Self->HexMngr.GetHexToDraw(hx,hy)) return;

	AnyFrames* anim=Self->AnimGetFrames(spr_id);
	if(!anim || spr_index>=anim->GetCnt()) return;

	ProtoItem* proto_item=ItemMngr.GetProtoItem(proto_id);
	DWORD pos=HEX_POS(hx,hy);
	bool is_flat=(proto_item?FLAG(proto_item->Flags,ITEM_FLAT):false);
	bool is_scen=(proto_item?proto_item->IsScen():false);
	bool no_light=(is_flat && is_scen);

	Field& f=Self->HexMngr.GetField(hx,hy);
	Sprites& tree=Self->HexMngr.GetDrawTree();
	Sprite& spr=tree.InsertSprite(is_flat?DRAW_ORDER_ITEM_FLAT(is_scen):DRAW_ORDER_ITEM(pos),
		f.ScrX+16+ox,f.ScrY+6+oy,spr_index<0?anim->GetCurSprId():anim->GetSprId(spr_index),
		NULL,NULL,NULL,NULL,no_light?NULL:Self->HexMngr.GetLightHex(hx,hy),NULL);

	if(proto_item) // Copy from void ItemHex::SetEffects(Sprite* prep);
	{
		if(FLAG(proto_item->Flags,ITEM_COLORIZE))
		{
			spr.Alpha=((BYTE*)&proto_item->LightColor)+3;
			spr.Color=proto_item->LightColor&0xFFFFFF;
		}

		if(is_flat || proto_item->DisableEgg) spr.Egg=Sprite::EggNone;
		else
		{
			switch(proto_item->Corner)
			{
			case CORNER_SOUTH: spr.Egg=Sprite::EggXorY;
			case CORNER_NORTH: spr.Egg=Sprite::EggXandY;
			case CORNER_EAST_WEST:
			case CORNER_WEST: spr.Egg=Sprite::EggY;
			default: spr.Egg=Sprite::EggX; // CORNER_NORTH_SOUTH, CORNER_EAST
			}
		}

		if(proto_item->Flags&ITEM_BAD_ITEM) spr.Contour=Sprite::ContourRed;
	}
}

void FOMapper::SScriptFunc::Global_DrawCritter2d(DWORD crtype, DWORD anim1, DWORD anim2, BYTE dir, int l, int t, int r, int b, bool scratch, bool center, DWORD color)
{
	if(CritType::IsEnabled(crtype))
	{
		AnyFrames* frm=CritterCl::LoadAnim(crtype,anim1,anim2,dir);
		if(frm) Self->SprMngr.DrawSpriteSize(frm->Ind[0],l,t,r-l,b-t,scratch,center,color?color:COLOR_IFACE);
	}
}

Animation3dVec DrawCritter3dAnim;
DwordVec DrawCritter3dCrType;
DwordVec DrawCritter3dFailToLoad;
int DrawCritter3dLayers[LAYERS3D_COUNT];
void FOMapper::SScriptFunc::Global_DrawCritter3d(DWORD instance, DWORD crtype, DWORD anim1, DWORD anim2, asIScriptArray* layers, asIScriptArray* position, DWORD color)
{
	// x y
	// rx ry rz
	// sx sy sz
	// speed
	// stencil l t r b
	if(CritType::IsEnabled(crtype))
	{
		if(instance>=DrawCritter3dAnim.size())
		{
			DrawCritter3dAnim.resize(instance+1);
			DrawCritter3dCrType.resize(instance+1);
			DrawCritter3dFailToLoad.resize(instance+1);
		}

		if(DrawCritter3dFailToLoad[instance] && DrawCritter3dCrType[instance]==crtype) return;

		Animation3d*& anim=DrawCritter3dAnim[instance];
		if(!anim || DrawCritter3dCrType[instance]!=crtype)
		{
			SAFEDEL(anim);
			anim=Animation3d::GetAnimation(Str::Format("%s.fo3d",CritType::GetName(crtype)),PT_ART_CRITTERS,false);
			DrawCritter3dCrType[instance]=crtype;
			DrawCritter3dFailToLoad[instance]=0;

			if(!anim)
			{
				DrawCritter3dFailToLoad[instance]=1;
				return;
			}
			anim->EnableShadow(false);
		}

		if(anim)
		{
			DWORD count=(position?position->GetElementCount():0);
			float x=(count>0?*(float*)position->GetElementPointer(0):0.0f);
			float y=(count>1?*(float*)position->GetElementPointer(1):0.0f);
			float rx=(count>2?*(float*)position->GetElementPointer(2):0.0f);
			float ry=(count>3?*(float*)position->GetElementPointer(3):0.0f);
			float rz=(count>4?*(float*)position->GetElementPointer(4):0.0f);
			float sx=(count>5?*(float*)position->GetElementPointer(5):1.0f);
			float sy=(count>6?*(float*)position->GetElementPointer(6):1.0f);
			float sz=(count>7?*(float*)position->GetElementPointer(7):1.0f);
			float speed=(count>8?*(float*)position->GetElementPointer(8):1.0f);
			// 9 reserved
			float stl=(count>10?*(float*)position->GetElementPointer(10):0.0f);
			float stt=(count>11?*(float*)position->GetElementPointer(11):0.0f);
			float str=(count>12?*(float*)position->GetElementPointer(12):0.0f);
			float stb=(count>13?*(float*)position->GetElementPointer(13):0.0f);

			ZeroMemory(DrawCritter3dLayers,sizeof(DrawCritter3dLayers));
			for(DWORD i=0,j=(layers?layers->GetElementCount():0);i<j && i<LAYERS3D_COUNT;i++)
				DrawCritter3dLayers[i]=*(int*)layers->GetElementPointer(i);

			anim->SetRotation(rx*D3DX_PI/180.0f,ry*D3DX_PI/180.0f,rz*D3DX_PI/180.0f);
			anim->SetScale(sx,sy,sz);
			anim->SetSpeed(speed);
			anim->SetAnimation(anim1,anim2,DrawCritter3dLayers,0);
			Self->SprMngr.Draw3d(x,y,1.0f,anim,stl<str && stt<stb?&FLTRECT(stl,stt,str,stb):NULL,color?color:COLOR_IFACE);
		}
	}
}
















