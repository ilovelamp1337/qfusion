#ifndef QFUSION_UIFACADE_H
#define QFUSION_UIFACADE_H

#include "Logger.h"
#include "MessagePipe.h"
#include "Ipc.h"
#include "RendererCompositionProxy.h"

#include "../gameshared/q_math.h"

#include <string>
#include <include/cef_render_handler.h>

class CefBrowser;
class WswCefRenderHandler;

class BrowserProcessLogger: public Logger {
	void SendLogMessage( cef_log_severity_t severity, const char *format, va_list va ) override;
};

class ItemDrawParams {
public:
	virtual ~ItemDrawParams() = default;
	virtual const float *TopLeft() const = 0;
	virtual const float *Dimensions() const = 0;
	virtual int16_t ZIndex() const = 0;
};

class ModelDrawParams: public virtual ItemDrawParams {
public:
	virtual const CefString &Model() const = 0;
	virtual const CefString &Skin() const = 0;
	virtual int ColorRgba() const  = 0;
	virtual bool IsAnimLooping() const = 0;
	virtual const std::vector<ViewAnimFrame> &AnimFrames() const = 0;
};

class ImageDrawParams: public virtual ItemDrawParams {
public:
	virtual const CefString &Shader() const  = 0;
};

class UiFacade {
	friend class MessagePipe;
	friend class WswCefRenderHandler;
	friend class RendererCompositionProxy;

	CefRefPtr<CefBrowser> browser;
	WswCefRenderHandler *renderHandler;

	int width;
	int height;

	MainScreenState *thisFrameScreenState { nullptr };

	int demoProtocol;
	std::string demoExtension;
	std::string basePath;

	MessagePipe messagePipe;

	RendererCompositionProxy rendererCompositionProxy;

	UiFacade( int width_, int height_, int demoProtocol_, const char *demoExtension_, const char *basePath_ );
	~UiFacade();

	static void MenuOpenHandler() { Instance()->MenuCommand(); }
	static void MenuCloseHandler() { Instance()->MenuCommand(); }
	static void MenuForceHandler() { Instance()->MenuCommand(); }
	static void MenuModalHandler() { Instance()->MenuCommand(); }

	void MenuCommand();

	static UiFacade *instance;

	CefBrowser *GetBrowser() { return browser.get(); }

	static bool InitCef( int argc, char **argv, void *hInstance, int width, int height );

	int64_t lastRefreshAt;
	int64_t lastScrollAt;
	int numScrollsInARow;
	int lastScrollDirection;

	int mouseXY[2] { 0, 0 };

	uint32_t GetInputModifiers() const;
	bool ProcessAsMouseKey( int context, int qKey, bool down );
	void AddToScroll( int context, int direction );

	struct cvar_s *menu_sensitivity { nullptr };
	struct cvar_s *menu_mouseAccel { nullptr };

	BrowserProcessLogger logger;

	void OnRendererDeviceAcquired( int newWidth, int newHeight );
public:
	static void Shutdown() {
		delete instance;
		instance = nullptr;
	}

	static void InitOrAcquireDevice();

	static UiFacade *Instance() { return instance; }

	Logger *Logger() { return &logger; }

	void RegisterBrowser( CefRefPtr<CefBrowser> browser_ );
	void UnregisterBrowser( CefRefPtr<CefBrowser> browser_ );

	void OnUiPageReady() {
		messagePipe.OnUiPageReady();
	}

	int Width() const { return width; }
	int Height() const { return height; }

	void Refresh( int64_t time, int clientState, int serverState,
				  bool demoPlaying, const char *demoName, bool demoPaused,
				  unsigned int demoTime, bool backGround, bool showCursor );

	void UpdateConnectScreen( const char *serverName, const char *rejectMessage,
							  int downloadType, const char *downloadFilename,
							  float downloadPercent, int downloadSpeed,
							  int connectCount, bool backGround );

	void Keydown( int context, int qKey );

	void Keyup( int context, int qKey );

	void CharEvent( int context, int qKey );

	void MouseMove( int context, int frameTime, int dx, int dy );

	void MouseSet( int context, int mx, int my, bool showCursor ) {
		messagePipe.MouseSet( context, mx, my, showCursor );
	}

	void ForceMenuOn();

	void ForceMenuOff() {
		messagePipe.ForceMenuOff();
	}

	void ShowQuickMenu( bool show ) {
		messagePipe.ShowQuickMenu( show );
	}

	bool HaveQuickMenu() {
		return false;
	}

	bool TouchEvent( int context, int id, int type, int x, int y ) {
		// Unsupported!
		return false;
	}

	bool IsTouchDown( int context, int id ) {
		// Unsupported!
		return false;
	}

	void CancelTouches( int context ) {}

	void AddToServerList( const char *adr, const char *info ) {
		// TODO: This is kept just to provide some procedure address
		// Ask UI/frontend folks what do they expect
	}

	static void OnRendererDeviceLost() {
		if( instance ) {
			instance->rendererCompositionProxy.OnRendererDeviceLost();
		}
	}

	int StartDrawingModel( const ModelDrawParams &params ) {
		return rendererCompositionProxy.StartDrawingModel( params );
	}

	int StartDrawingImage( const ImageDrawParams &params ) {
		return rendererCompositionProxy.StartDrawingImage( params );
	}

	bool StopDrawingModel( int drawnModelHandle ) {
		return rendererCompositionProxy.StopDrawingModel( drawnModelHandle );
	}

	bool StopDrawingImage( int drawnImageHandle ) {
		return rendererCompositionProxy.StopDrawingImage( drawnImageHandle );
	}
};

#endif
