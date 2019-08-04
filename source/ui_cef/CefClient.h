#ifndef UI_CEF_CLIENT_H
#define UI_CEF_CLIENT_H

#include "UiFacade.h"
#include "Ipc.h"

#include "include/cef_client.h"
#include "include/cef_life_span_handler.h"
#include "include/cef_render_handler.h"

class WswCefRenderHandler: public CefRenderHandler {
	RendererCompositionProxy *const rendererCompositionProxy;

	void FillRect( CefRect &rect ) {
		rect.x = 0;
		rect.y = 0;
		rect.width = rendererCompositionProxy->Width();
		rect.height = rendererCompositionProxy->Height();
	}
public:
	explicit WswCefRenderHandler( UiFacade *facade )
		: rendererCompositionProxy( &facade->rendererCompositionProxy ) {}

	bool GetRootScreenRect( CefRefPtr<CefBrowser> browser, CefRect& rect ) override {
		FillRect( rect );
		return true;
	}

	bool GetViewRect( CefRefPtr<CefBrowser> browser, CefRect& rect ) override {
		FillRect( rect );
		return true;
	}

	void OnPaint( CefRefPtr<CefBrowser> browser, PaintElementType type, const RectList& dirtyRects,
				  const void* buffer, int width_, int height_ ) override {
		rendererCompositionProxy->UpdateChromiumBuffer( dirtyRects, buffer, width_, height_ );
	}

	IMPLEMENT_REFCOUNTING( WswCefRenderHandler );
};

class WswCefDisplayHandler: public CefDisplayHandler {
	bool OnConsoleMessage( CefRefPtr<CefBrowser> browser,
						   const CefString& message,
						   const CefString& source,
						   int line ) override;

	UiFacade *const parent;
public:
	explicit WswCefDisplayHandler( UiFacade *parent_ ): parent( parent_ ) {}

	IMPLEMENT_REFCOUNTING( WswCefDisplayHandler );
};

class WswCefClient: public CefClient, public CefLifeSpanHandler, public CefContextMenuHandler {
	friend class CallbackRequestHandler;

	CallbackRequestHandler *requestHandlersHead;

	GetCVarRequestHandler getCVar;
	SetCVarRequestHandler setCVar;
	ExecuteCmdRequestHandler executeCmd;
	GetVideoModesRequestHandler getVideoModes;
	GetDemosAndSubDirsRequestHandler getDemosAndSubDirs;
	GetDemoMetaDataRequestHandler getDemoMetaData;
	GetHudsRequestHandler getHuds;
	GetGametypesRequestHandler getGametypes;
	GetMapsRequestHandler getMaps;
	GetLocalizedStringsRequestHandler getLocalizedStrings;
	GetKeyBindingsRequestHandler getKeyBindings;
	GetKeyNamesRequestHandler getKeyNames;
	StartDrawingModelRequestHandler startDrawingModel;
	StopDrawingModelRequestHandler stopDrawingModel;
	StartDrawingImageRequestHandler startDrawingImage;
	StopDrawingImageRequestHandler stopDrawingImage;

	CefRefPtr<WswCefRenderHandler> renderHandler;
	CefRefPtr<WswCefDisplayHandler> displayHandler;

	inline Logger *Logger() { return UiFacade::Instance()->Logger(); }
public:
	WswCefClient( int width, int height )
		: requestHandlersHead( nullptr )
		, getCVar( this )
		, setCVar( this )
		, executeCmd( this )
		, getVideoModes( this )
		, getDemosAndSubDirs( this )
		, getDemoMetaData( this )
		, getHuds( this )
		, getGametypes( this )
		, getMaps( this )
		, getLocalizedStrings( this )
		, getKeyBindings( this )
		, getKeyNames( this )
		, startDrawingModel( this )
		, stopDrawingModel( this )
		, startDrawingImage( this )
		, stopDrawingImage( this )
		, renderHandler( new WswCefRenderHandler( UiFacade::Instance() ) )
		, displayHandler( new WswCefDisplayHandler( UiFacade::Instance() ) ) {}

	CefRefPtr<CefRenderHandler> GetRenderHandler() override {
		return renderHandler;
	}

	CefRefPtr<CefDisplayHandler> GetDisplayHandler() override {
		return displayHandler;
	}

	CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override {
		return this;
	}

	CefRefPtr<CefContextMenuHandler> GetContextMenuHandler() override {
		return this;
	}

	void OnBeforeContextMenu( CefRefPtr<CefBrowser> browser,
							  CefRefPtr<CefFrame> frame,
							  CefRefPtr<CefContextMenuParams> params,
							  CefRefPtr<CefMenuModel> model ) override {
		// Disable the menu...
		model->Clear();
	}

	void OnAfterCreated( CefRefPtr<CefBrowser> browser ) override {
		UiFacade::Instance()->RegisterBrowser( browser );
	}

	void OnBeforeClose( CefRefPtr<CefBrowser> browser ) override {
		UiFacade::Instance()->UnregisterBrowser( browser );
	}

	bool OnProcessMessageReceived( CefRefPtr<CefBrowser> browser,
								   CefProcessId source_process,
								   CefRefPtr<CefProcessMessage> message ) override;

	IMPLEMENT_REFCOUNTING( WswCefClient );
};

#endif
