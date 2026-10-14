#include "AssetRegistry/AssetRegistryModule.h"
#include "Containers/Ticker.h"
#include "FileHelpers.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Internationalization/Regex.h"
#include "Framework/Docking/TabManager.h"
#include "Misc/EngineVersion.h"
#include "Misc/Guid.h"
#include "SWebBrowser.h"
#include "Server/BridgeLog.h"
#include "Server/JsonUtil.h"
#include "Server/ToolRegistry.h"
#include "Tools/FabPageBridge.h"
#include "Tools/ToolRegistration.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"
#include "Widgets/Docking/SDockTab.h"

using namespace UCli;

void UFabPageBridge::Report(const FString& Id, const FString& Json)
{
	Results.Add(Id, Json);
}

namespace
{
	const FName FabTabId(TEXT("FabTab"));
	const TCHAR* SignInHint = TEXT("open the Fab tab (Window > Fab), sign in, and retry");

	UFabPageBridge* PageBridge()
	{
		static TStrongObjectPtr<UFabPageBridge> Bridge;
		if (!Bridge.IsValid())
		{
			Bridge.Reset(NewObject<UFabPageBridge>(GetTransientPackage(), TEXT("UnrealCliFabPageBridge")));
		}
		return Bridge.Get();
	}

	TSharedPtr<SWebBrowser> FindBrowser(const TSharedRef<SWidget>& Widget)
	{
		if (Widget->GetType() == FName(TEXT("SWebBrowser")))
		{
			return StaticCastSharedRef<SWebBrowser>(Widget);
		}
		FChildren* Children = Widget->GetChildren();
		for (int32 i = 0; Children && i < Children->Num(); ++i)
		{
			if (TSharedPtr<SWebBrowser> Found = FindBrowser(Children->GetChildAt(i)))
			{
				return Found;
			}
		}
		return nullptr;
	}

	/** The Fab tab's browser, opening the tab when needed. */
	TSharedPtr<SWebBrowser> FabBrowser(bool bOpen)
	{
		TSharedPtr<SDockTab> Tab = FGlobalTabmanager::Get()->FindExistingLiveTab(FTabId(FabTabId));
		if (!Tab && bOpen && FGlobalTabmanager::Get()->HasTabSpawner(FabTabId))
		{
			Tab = FGlobalTabmanager::Get()->TryInvokeTab(FTabId(FabTabId));
		}
		return Tab ? FindBrowser(Tab->GetContent()) : nullptr;
	}

	bool FabAvailable(const FToolCallRef& Call)
	{
		if (FGlobalTabmanager::Get()->HasTabSpawner(FabTabId))
		{
			return true;
		}
		Call->Fail(TEXT("fab.unavailable"), TEXT("the Fab plugin is not loaded in this editor"), TEXT("enable the Fab plugin (Edit > Plugins > Fab) and restart the editor"));
		return false;
	}

	struct FPageRun
	{
		FString Id;
		double StartedAt = 0;
		double SentAt = 0;
		double Limit = 60;
		bool bSent = false;
		bool bReloaded = false;
	};

	using FOnPageValue = TFunction<void(const TSharedPtr<FJsonValue>&)>;

	/**
	 * Runs Script (an async function body; `A` holds Args) in the Fab page and hands its return value to OnValue.
	 * A script fails with a "fab.<code>: message" error to choose the error code.
	 */
	void RunInPage(const FToolCallRef& Call, const FString& Script, const TSharedPtr<FJsonObject>& Args, double Limit, FOnPageValue OnValue)
	{
		TSharedRef<FPageRun> Run = MakeShared<FPageRun>();
		Run->Id = FGuid::NewGuid().ToString(EGuidFormats::Digits);
		Run->StartedAt = FPlatformTime::Seconds();
		Run->Limit = FMath::Max(5.0, Limit);
		const FString Wrapped = FString::Printf(
			TEXT("(async()=>{const __r=(v)=>window.ue.unrealcliagent.report('%s',JSON.stringify(v===undefined?null:v));")
			TEXT("try{const A=%s;const __v=await (async()=>{%s\n})();__r({ok:true,value:__v});}")
			TEXT("catch(e){const m=e&&e.message||'';__r({ok:false,error:m.startsWith('fab.')?m:String(e&&e.stack||e)});}})();"),
			*Run->Id, *ToJson(Args.IsValid() ? Args : MakeShared<FJsonObject>()), *Script);
		FabBrowser(true);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Call, Run, Wrapped, OnValue](float)
		{
			const double Now = FPlatformTime::Seconds();
			UFabPageBridge* Bridge = PageBridge();
			if (FString* Json = Bridge->Results.Find(Run->Id))
			{
				const TSharedPtr<FJsonObject> Out = ParseObject(*Json);
				Bridge->Results.Remove(Run->Id);
				if (Out && Out->GetBoolField(TEXT("ok")))
				{
					OnValue(Out->TryGetField(TEXT("value")));
					return false;
				}
				const FString Error = Out ? Out->GetStringField(TEXT("error")) : *Json;
				FString Code = TEXT("fab.script_error"), Message = Error;
				if (Error.StartsWith(TEXT("fab.")))
				{
					Error.Split(TEXT(": "), &Code, &Message);
				}
				Call->Fail(Code, Message, Code == TEXT("fab.script_error") ? TEXT("fix the script; the page may also require signing in to Fab") : FString());
				return false;
			}
			if (Call->bCancelled || Now - Run->StartedAt > Run->Limit)
			{
				Call->Fail(TEXT("fab.timeout"), FString::Printf(TEXT("the Fab page did not answer within %.0f s"), Run->Limit),
					TEXT("check that the Fab tab shows the store and that you are signed in, then retry"));
				return false;
			}
			TSharedPtr<SWebBrowser> Browser = FabBrowser(true);
			if (!Browser || !Browser->IsLoaded())
			{
				return true;
			}
			if (!Run->bSent)
			{
				Browser->BindUObject(TEXT("unrealcliagent"), Bridge, true);
				Browser->ExecuteJavascript(Wrapped);
				Run->bSent = true;
				Run->SentAt = Now;
				return true;
			}
			// A binding made after the page loaded only appears after a reload.
			if (!Run->bReloaded && Now - Run->SentAt > 3.0)
			{
				Browser->Reload();
				Run->bReloaded = true;
				Run->bSent = false;
			}
			return true;
		}), 0.1f);
	}

	void SucceedWithValue(const FToolCallRef& Call, const TSharedPtr<FJsonValue>& V)
	{
		const TSharedPtr<FJsonObject>* O = nullptr;
		if (V.IsValid() && V->TryGetObject(O))
		{
			Call->Succeed(*O);
		}
		else
		{
			Call->Succeed(V.IsValid() ? V : MakeShared<FJsonValueNull>());
		}
	}

	/** The Fab plugin's record of imported listings: content path -> listing id. */
	TMap<FString, FString> ImportedListings(const TCHAR* RemoveListing = nullptr)
	{
		TMap<FString, FString> Out;
		UClass* Cls = FindObject<UClass>(nullptr, TEXT("/Script/Fab.FabLocalAssets"));
		FMapProperty* Prop = Cls ? FindFProperty<FMapProperty>(Cls, TEXT("PathsListingID")) : nullptr;
		if (!Prop || !CastField<FStrProperty>(Prop->KeyProp) || !CastField<FStrProperty>(Prop->ValueProp))
		{
			return Out;
		}
		FScriptMapHelper Map(Prop, Prop->ContainerPtrToValuePtr<void>(Cls->GetDefaultObject()));
		TArray<FString> Remove;
		for (int32 i = 0; i < Map.GetMaxIndex(); ++i)
		{
			if (Map.IsValidIndex(i))
			{
				const FString& Path = *reinterpret_cast<const FString*>(Map.GetKeyPtr(i));
				const FString& Listing = *reinterpret_cast<const FString*>(Map.GetValuePtr(i));
				if (RemoveListing && Listing == RemoveListing)
				{
					Remove.Add(Path);
				}
				else
				{
					Out.Add(Path, Listing);
				}
			}
		}
		for (const FString& Path : Remove)
		{
			Map.RemovePair(&Path);
		}
		return Out;
	}

	/** Top-level content folders on disk (as /Game/<Folder>), without the World Partition external-object folders. */
	TSet<FString> ContentFolders()
	{
		TSet<FString> Out;
		IFileManager::Get().IterateDirectory(*FPaths::ProjectContentDir(), [&Out](const TCHAR* Name, bool bIsDirectory)
		{
			const FString Folder = FPaths::GetCleanFilename(Name);
			if (bIsDirectory && !Folder.StartsWith(TEXT("__External")))
			{
				Out.Add(TEXT("/Game/") + Folder);
			}
			return true;
		});
		return Out;
	}

	const TCHAR* SearchScript = TEXT(R"JS(
const p=new URLSearchParams({q:A.query||'',count:String(Math.min(A.count||20,100))});
const r=await fetch('/i/listings/search?'+p,{headers:{accept:'application/json'}});
if(!r.ok) throw new Error(`fab.request_failed: search returned HTTP ${r.status}`);
const j=await r.json();
let items=j.results.map(x=>({uid:x.uid,title:x.title,type:x.listingType,seller:x.user&&x.user.sellerName,isFree:!!x.isFree,
  formats:(x.assetFormats||[]).map(f=>f.assetFormatType.code),rating:x.averageRating??null,url:`https://www.fab.com/listings/${x.uid}`}));
if(A.freeOnly) items=items.filter(i=>i.isFree);
return {count:items.length,items};
)JS");

	const TCHAR* LibraryScript = TEXT(R"JS(
const max=A.count||100; const q={sort_by:'-createdAt',count:'100'}; if(A.query) q.q=A.query;
let url='/i/library/search?'+new URLSearchParams(q); const items=[];
while(url&&items.length<max){
  const r=await fetch(url,{headers:{accept:'application/json'}});
  if(r.status===401||r.status===403) throw new Error('fab.not_signed_in: the Fab page is not signed in');
  if(!r.ok) throw new Error(`fab.request_failed: library returned HTTP ${r.status}`);
  const j=await r.json();
  for(const x of j.results){ const l=x.listing; items.push({uid:l.uid,title:l.title,type:l.listingType,seller:l.publisher&&l.publisher.sellerName,
    formats:(l.assetFormats||[]).map(f=>f.assetFormatType.code),canDownload:!!x.canRequestDownloadUrl}); }
  url=j.next?(()=>{const u=new URL(j.next);return u.pathname+u.search;})():null;
}
return {count:Math.min(items.length,max),items:items.slice(0,max)};
)JS");

	const TCHAR* StatusScript = TEXT(R"JS(
const r=await fetch('/i/users/me',{headers:{accept:'application/json'}});
const me=r.ok?await r.json():null;
return {signedIn:!!(me&&me.epicId),displayName:me&&me.displayName||null,url:location.href};
)JS");

	const TCHAR* AddScript = TEXT(R"JS(
const get=async(p)=>{const r=await fetch(p,{headers:{accept:'application/json'}});const t=await r.text();let v=null;try{v=JSON.parse(t);}catch(e){}return {status:r.status,v,t};};
const m=String(A.listing||'').match(/[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}/i);
if(!m) throw new Error('fab.bad_listing: pass a listing id or a fab.com listing URL');
const uid=m[0].toLowerCase();
const me=await get('/i/users/me'); if(me.status!==200) throw new Error('fab.not_signed_in: the Fab page is not signed in');
const L=await get(`/i/listings/${uid}`);
if(L.status!==200||!L.v) throw new Error(`fab.not_found: listing ${uid} was not found (HTTP ${L.status})`);
const listing=L.v;
const codes=(listing.assetFormats||[]).map(f=>f.assetFormatType.code);
const importable=['unreal-engine','gltf','glb','fbx'];
const code=(A.format?[A.format]:importable).find(c=>codes.includes(c)&&importable.includes(c));
if(!code) throw new Error(`fab.unsupported_format: ${A.format?`format ${A.format} is not offered or not importable`:'no importable format'}; offered: ${codes.join(', ')}; importable: ${importable.join(', ')}`);
const isQuixel=/quixel/i.test(listing.user&&listing.user.sellerName||'');
const F=await get(`/i/listings/${uid}/asset-formats/${code}`);
if(F.status!==200||!F.v) throw new Error(`fab.request_failed: format ${code} returned HTTP ${F.status}`);
let fileUid,fileName,engineVersions=null;
if(code==='unreal-engine'){
  const dm=F.v.distributionMethod;
  if(dm&&dm!=='asset_pack') throw new Error(`fab.unsupported_format: this listing is distributed as ${dm}; only asset packs can be added to a project (install plugins and complete projects from the Epic Games Launcher)`);
  const all=F.v.versions||[];
  const vs=all.filter(v=>v.status==='ready'&&(v.engineVersions||[]).includes(A.engine));
  if(!vs.length) throw new Error(`fab.engine_mismatch: no version for ${A.engine}; available: ${[...new Set(all.flatMap(v=>v.engineVersions||[]))].join(', ')||'none'}`);
  fileUid=vs[0].uid; fileName=vs[0].name; engineVersions=vs[0].engineVersions;
}else{
  const files=F.v.files||[];
  const f=(isQuixel&&files.find(x=>new RegExp(`_${A.quality||'high'}\\.zip$`,'i').test(x.name)))
    ||files.find(x=>x.name.toLowerCase().endsWith('.'+code))||files.find(x=>/\.zip$/i.test(x.name))||files[0];
  if(!f) throw new Error(`fab.request_failed: format ${code} has no files`);
  fileUid=f.uid; fileName=f.name;
}
const dl=`/i/listings/${uid}/asset-formats/${code}/files/${fileUid}/download-info?platform=${A.platform}`;
let D=await get(dl); let acquired=false;
if(D.status!==200){
  if(!A.acquireFree) throw new Error(`fab.not_owned: "${listing.title}" is not in your Fab library (HTTP ${D.status}); add it on fab.com, or pass acquireFree for a free listing`);
  if(!listing.isFree||!(listing.startingPrice&&listing.startingPrice.offerId)) throw new Error(`fab.not_free: "${listing.title}" is not free; buy it on fab.com first`);
  const csrf=(document.cookie.match(/(?:^|; )fab_csrftoken=([^;]+)/)||[])[1]||'';
  const body=new FormData(); body.append('offer_id',listing.startingPrice.offerId);
  const r=await fetch(`/i/listings/${uid}/add-to-library`,{method:'POST',body,headers:{'X-CsrfToken':decodeURIComponent(csrf),'X-Requested-With':'XMLHttpRequest'}});
  if(!r.ok) throw new Error(`fab.acquire_failed: adding to the library returned HTTP ${r.status}: ${(await r.text()).slice(0,300)}`);
  acquired=true; D=await get(dl);
}
if(D.status!==200||!D.v||!(D.v.downloadInfo||[]).length) throw new Error(`fab.download_failed: download-info returned HTTP ${D.status}: ${D.t.slice(0,300)}`);
const d=D.v.downloadInfo[0];
window.ue.fab.addtoproject(d.downloadUrl,{assetNamespace:'fab',assetId:uid,assetType:code,assetName:listing.title,listingType:listing.listingType,isQuixel,distributionPointBaseUrls:d.distributionPointBaseUrls||[]});
return {listingUid:uid,title:listing.title,format:code,file:fileName,engineVersions,acquired};
)JS");

	TSharedPtr<FJsonObject> PageArgs(const FToolCallRef& Call)
	{
		TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>();
		for (const TPair<FString, TSharedPtr<FJsonValue>>& P : Call->Args->Values)
		{
			A->SetField(P.Key, P.Value);
		}
		A->SetStringField(TEXT("engine"), FString::Printf(TEXT("UE_%d.%d"), FEngineVersion::Current().GetMajor(), FEngineVersion::Current().GetMinor()));
		A->SetStringField(TEXT("platform"), PLATFORM_WINDOWS ? TEXT("Windows") : PLATFORM_MAC ? TEXT("Mac") : TEXT("Linux"));
		return A;
	}

	void RegisterPageTool(FToolRegistry& R, const TCHAR* Name, const TCHAR* Summary, const TCHAR* Schema, const TCHAR* Script, double Limit)
	{
		R.Add(Name, TEXT("fab"), Summary, Schema, EToolFlags::None,
			[Script, Limit](const FToolCallRef& Call)
			{
				if (FabAvailable(Call))
				{
					RunInPage(Call, Script, PageArgs(Call), Call->GetNumber(TEXT("timeoutSeconds"), Limit), [Call](const TSharedPtr<FJsonValue>& V) { SucceedWithValue(Call, V); });
				}
			});
	}
}

void UCli::RegisterFabTools(FToolRegistry& R)
{
	R.Add(TEXT("fab_page_run"), TEXT("fab"),
		TEXT("Run JavaScript in the editor's Fab browser page (opens the Fab tab). The script is an async function body; `A` holds the args; its return value comes back as JSON."),
		TEXT(R"({"type":"object","properties":{"script":{"type":"string"},"timeoutSeconds":{"type":"number"}},"required":["script"]})"), EToolFlags::NoUndo,
		[](const FToolCallRef& Call)
		{
			if (FabAvailable(Call))
			{
				RunInPage(Call, Call->GetString(TEXT("script")), PageArgs(Call), Call->GetNumber(TEXT("timeoutSeconds"), 60.0), [Call](const TSharedPtr<FJsonValue>& V)
				{
					TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
					O->SetField(TEXT("value"), V.IsValid() ? V : MakeShared<FJsonValueNull>());
					Call->Succeed(O);
				});
			}
		});

	RegisterPageTool(R, TEXT("fab_status"), TEXT("Whether the Fab plugin is loaded and its page is signed in to Fab (opens the Fab tab)."),
		TEXT(R"({"type":"object","properties":{}})"), StatusScript, 60.0);

	RegisterPageTool(R, TEXT("fab_search"), TEXT("Search Fab listings: uid, title, type, seller, isFree, formats. Pass a uid to fab_add."),
		TEXT(R"({"type":"object","properties":{"query":{"type":"string"},"count":{"type":"integer"},"freeOnly":{"type":"boolean"}},"required":["query"]})"), SearchScript, 60.0);

	RegisterPageTool(R, TEXT("fab_library"), TEXT("List the signed-in user's Fab library (owned listings), optionally filtered by query. Pass a uid to fab_add."),
		TEXT(R"({"type":"object","properties":{"query":{"type":"string"},"count":{"type":"integer"}}})"), LibraryScript, 120.0);

	R.Add(TEXT("fab_add"), TEXT("fab"),
		TEXT("Download a Fab listing from the user's library and add it to the project, as the Fab tab's Add to Project does (asset packs for this engine version, or glTF/GLB/FBX imported under /Game/Fab). Runs as a job; the result lists the imported content path. acquireFree adds a free listing to the user's library first (accepting its license); paid listings are never bought."),
		TEXT(R"({"type":"object","properties":{"listing":{"type":"string","description":"listing uid or fab.com listing URL"},"format":{"type":"string","enum":["unreal-engine","gltf","glb","fbx"]},"quality":{"type":"string","enum":["low","medium","high","raw"]},"acquireFree":{"type":"boolean"},"timeoutSeconds":{"type":"number"}},"required":["listing"]})"),
		EToolFlags::Mutating | EToolFlags::NoUndo | EToolFlags::LongRunning,
		[](const FToolCallRef& Call)
		{
			if (!FabAvailable(Call))
			{
				return;
			}
			if (!FindObject<UClass>(nullptr, TEXT("/Script/Fab.FabLocalAssets")))
			{
				Call->Fail(TEXT("fab.unavailable"), TEXT("this Fab plugin version is not supported (FabLocalAssets not found)"));
				return;
			}
			FRegexMatcher Uid(FRegexPattern(TEXT("[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}")), Call->GetString(TEXT("listing")));
			if (!Uid.FindNext())
			{
				Call->Fail(TEXT("fab.bad_listing"), TEXT("pass a listing id or a fab.com listing URL"), TEXT("find one with fab_search or fab_library"));
				return;
			}
			// Forget earlier imports of this listing so that this import's completion is observable.
			ImportedListings(*Uid.GetCaptureGroup(0).ToLower());
			const double Limit = FMath::Max(30.0, Call->GetNumber(TEXT("timeoutSeconds"), 1800.0));
			const int64 LogStart = FBridgeLog::Get().NextSeq();
			const TSet<FString> FoldersBefore = ContentFolders();
			RunInPage(Call, AddScript, PageArgs(Call), 120.0, [Call, Limit, LogStart, FoldersBefore](const TSharedPtr<FJsonValue>& V)
			{
				const TSharedPtr<FJsonObject>* Started = nullptr;
				if (!V.IsValid() || !V->TryGetObject(Started))
				{
					Call->Fail(TEXT("fab.script_error"), TEXT("the Fab page returned no listing"));
					return;
				}
				const TSharedPtr<FJsonObject> Info = *Started;
				const FString Listing = Info->GetStringField(TEXT("listingUid"));
				const double StartedAt = FPlatformTime::Seconds();
				FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Call, Info, Listing, Limit, LogStart, StartedAt, FoldersBefore](float)
				{
					FString Path;
					for (const TPair<FString, FString>& P : ImportedListings())
					{
						if (P.Value == Listing)
						{
							Path = P.Key;
						}
					}
					TArray<FString> Errors;
					bool bCancelled = false;
					for (const FBridgeLogLine& L : FBridgeLog::Get().Read(LogStart, 0, ELogVerbosity::Log, TEXT("LogFab")))
					{
						if (L.Verbosity <= ELogVerbosity::Error && !L.Text.Contains(TEXT("auth token")))
						{
							Errors.Add(L.Text);
						}
						bCancelled |= L.Text.Contains(TEXT("Import Cancelled")) || L.Text.Contains(TEXT("already being processed"));
					}
					if (Path.IsEmpty())
					{
						if (!Errors.IsEmpty() || bCancelled)
						{
							Call->Fail(TEXT("fab.import_failed"), Errors.IsEmpty() ? TEXT("the import was cancelled") : Errors.Last(), TEXT("read the LogFab lines in `unrealengine-cli logs --category LogFab`"), Arr(StrArray(Errors)));
							return false;
						}
						if (Call->bCancelled || FPlatformTime::Seconds() - StartedAt > Limit)
						{
							Call->Fail(TEXT("fab.timeout"), FString::Printf(TEXT("the import did not finish within %.0f s (it may still finish in the editor)"), Limit));
							return false;
						}
						return true;
					}
					IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
					// Fab records the folder of the pack's first file, which can be __ExternalActors__; report the folders the import created.
					TArray<FString> Paths = ContentFolders().Difference(FoldersBefore).Array();
					Paths.RemoveAll([&Path](const FString& P) { return Path.StartsWith(P + TEXT("/")); });
					Paths.Sort();
					if (Paths.IsEmpty() || (!Path.StartsWith(TEXT("/Game/__External")) && !Paths.Contains(Path)))
					{
						Paths.Insert(Path, 0);
					}
					Registry.ScanPathsSynchronous(Paths, true);
					TArray<FAssetData> Assets;
					for (const FString& P : Paths)
					{
						Registry.GetAssetsByPath(FName(*P), Assets, true);
					}
					TArray<UPackage*> Dirty;
					TArray<FString> Names;
					for (const FAssetData& A : Assets)
					{
						if (UPackage* Pkg = FindPackage(nullptr, *A.PackageName.ToString()); Pkg && Pkg->IsDirty())
						{
							Dirty.AddUnique(Pkg);
						}
						if (Names.Num() < 50)
						{
							Names.Add(A.GetObjectPathString());
						}
					}
					if (!Dirty.IsEmpty())
					{
						UEditorLoadingAndSavingUtils::SavePackages(Dirty, true);
					}
					Info->SetStringField(TEXT("path"), Paths[0]);
					Info->SetArrayField(TEXT("paths"), StrArray(Paths));
					Info->SetNumberField(TEXT("assetCount"), Assets.Num());
					Info->SetArrayField(TEXT("assets"), StrArray(Names));
					Info->SetNumberField(TEXT("seconds"), FPlatformTime::Seconds() - StartedAt);
					Call->Succeed(Info);
					return false;
				}), 0.5f);
			});
		});
}
