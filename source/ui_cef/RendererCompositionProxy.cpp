#include "UiFacade.h"
#include "CefClient.h"

#include "../ref_gl/r_frontend.h"

#include <memory>

RendererCompositionProxy::RendererCompositionProxy( UiFacade *parent_ )
	: parent( parent_ ), width( parent_->Width() ), height( parent_->Height() ) {
	ResizeBuffer();
}

inline void RendererCompositionProxy::ResizeBuffer() {
	delete chromiumBuffer;
	const size_t numBytes = width * height * 4;
	chromiumBuffer = new uint8_t[numBytes];
	// Clear the image data so no junk is drawn on screen for a fraction of a second
	::memset( chromiumBuffer, 0, numBytes );
}

inline void RendererCompositionProxy::RegisterChromiumBufferShader() {
	chromiumShader = R_RegisterRawPic( "chromiumBufferShader", width, height, chromiumBuffer, 4, true );
}

inline void RendererCompositionProxy::ResetBackground() {
	hasStartedWorldModelLoading = false;
	hasSucceededWorldModelLoading = false;
}

int RendererCompositionProxy::StartDrawingModel( const ModelDrawParams &params ) {
	const auto zIndex = params.ZIndex();

	if( !zIndicesSet.TrySet( zIndex ) ) {
		parent->Logger()->Error( "Can't reserve a z-index for a model: z-index %d is already in use", (int)zIndex );
		return 0;
	}

	DrawnAliasModel *newDrawnModel;
	try {
		newDrawnModel = new DrawnAliasModel( this, params );
	} catch( ... ) {
		zIndicesSet.Unset( zIndex );
		throw;
	}

	return drawnItemsRegistry.LinkNewItem( newDrawnModel );
}

int RendererCompositionProxy::StartDrawingImage( const ImageDrawParams &params ) {
	const auto zIndex = params.ZIndex();

	if( !zIndicesSet.TrySet( params.ZIndex() ) ) {
		parent->Logger()->Error( "Can't reserve a zIndex for an image: z-index %d is already in use", (int)zIndex );
		return 0;
	}

	Drawn2DImage *newDrawnImage;
	try {
		newDrawnImage = new Drawn2DImage( this, params );
	} catch( ... ) {
		zIndicesSet.Unset( zIndex );
		throw;
	}

	return drawnItemsRegistry.LinkNewItem( newDrawnImage );
}

bool RendererCompositionProxy::StopDrawingItem( int drawnItemHandle, const std::type_info &itemTypeInfo ) {
	if( NativelyDrawnItem *item = drawnItemsRegistry.TryUnlinkItem( drawnItemHandle ) ) {
		// Check whether it really was an item of the needed type.
		// Handles are assumed to be unqiue for all drawn items (models or images).
		// We're trying to catch this contract violation.
		assert( typeid( item ) == itemTypeInfo );
		delete item;
		return true;
	}

	return false;
}

RendererCompositionProxy::DrawnItemsRegistry::~DrawnItemsRegistry() {
	NativelyDrawnItem *nextItem;
	for( auto *item = globalItemsListHead; item; item = nextItem ) {
		nextItem = item->NextInGlobalList();
		delete item;
	}
}

int RendererCompositionProxy::DrawnItemsRegistry::LinkNewItem( RendererCompositionProxy::NativelyDrawnItem *item ) {
	// The z-index must be already set
	assert( item->zIndex );
	const int handle = NextHandle();
	item->handle = handle;

	const unsigned hashBinIndex = HashBinIndex( handle );

	// Link the item to the global list (so it could be deallocated in destructor)
	Link( item, &globalItemsListHead, NativelyDrawnItem::GLOBAL_LIST );
	// Link the item to its hash bin
	Link( item, &hashBins[hashBinIndex], NativelyDrawnItem::BIN_LIST );

	if( item->zIndex > 0 ) {
		positiveZItemsList.AddItem( item );
	} else {
		negativeZItemsList.AddItem( item );
	}

	return handle;
}

RendererCompositionProxy::NativelyDrawnItem *RendererCompositionProxy::DrawnItemsRegistry::TryUnlinkItem( int itemHandle ) {
	if( !itemHandle ) {
		return nullptr;
	}

	const unsigned hashBinIndex = HashBinIndex( itemHandle );
	for( NativelyDrawnItem *item = hashBins[hashBinIndex]; item; item = item->NextInBinList() ) {
		if( item->handle == itemHandle ) {
			// Unlink from hash bin
			Unlink( item, &hashBins[hashBinIndex], NativelyDrawnItem::BIN_LIST );
			// Unlink from allocated list
			Unlink( item, &globalItemsListHead, NativelyDrawnItem::GLOBAL_LIST );
			// Remove from the corresponding half-space sorted list
			if( item->zIndex > 0 ) {
				positiveZItemsList.RemoveItem( item );
			} else {
				negativeZItemsList.RemoveItem( item );
			}
		}
	}

	return nullptr;
}

void RendererCompositionProxy::DrawnItemsRegistry::HalfSpaceDrawList::DrawItems( int64_t time ) {
	if( invalidated ) {
		BuildSortedList();
		invalidated = false;
	}

	// Draw items sorted by z-index
	for( SortedDrawnItemRef itemRef: sortedItems ) {
		itemRef.item->DrawSelf( time );
	}
}

void RendererCompositionProxy::DrawnItemsRegistry::HalfSpaceDrawList::BuildSortedList() {
	sortedItems.clear();
	sortedItems.reserve( (unsigned)numItems );

	// Heap sort feels more natural in this case but is really less efficient
	for( auto item = drawnItemsSetHead; item; item = item->next[DRAWN_LIST] ) {
		sortedItems.emplace_back( SortedDrawnItemRef( item ) );
	}

	std::sort( sortedItems.begin(), sortedItems.end() );
}

void RendererCompositionProxy::UpdateChromiumBuffer( const CefRenderHandler::RectList &dirtyRects,
													 const void *buffer, int w, int h ) {
	// Note: we currently HAVE to maintain a local copy of the buffer...
	// TODO: Avoid that and supply data directly to GPU if the shader is not invalidated

	const auto *in = (const uint8_t *)buffer;

	// Check whether we can copy the entire screen buffer or a continuous region of a screen buffer
	if( dirtyRects.size() == 1 ) {
		const auto &rect = dirtyRects[0];
		if( rect.width == w ) {
			assert( rect.x == 0 );
			const size_t screenRowSize = 4u * w;
			const ptrdiff_t startOffset = screenRowSize * rect.y;
			const size_t bytesToCopy = screenRowSize * rect.height;
			memcpy( this->chromiumBuffer + startOffset, in + startOffset, bytesToCopy );
			return;
		}
	}

	const size_t screenRowSize = 4u * w;
	for( const auto &rect: dirtyRects ) {
		ptrdiff_t rectRowOffset = screenRowSize * rect.y + 4 * rect.x;
		const size_t rowSize = 4u * rect.width;
		// There are rect.height rows in the copied rect
		for( int i = 0; i < rect.height; ++i ) {
			// TODO: Start prefetching next row?
			memcpy( this->chromiumBuffer + rectRowOffset, in + rectRowOffset, rowSize );
			rectRowOffset += screenRowSize;
		}
	}
}

void RendererCompositionProxy::Refresh( int64_t time, bool showCursor, bool background ) {
	if( isRendererDeviceLost ) {
		wasRendererDeviceLost = true;
		return;
	}

	// Ok it seems we have to touch these shaders every frame as they are invalidated on map loading
	whiteShader = R_RegisterPic( "$whiteimage" );
	cursorShader = R_RegisterPic( "gfx/ui/cursor.tga" );

	if( background ) {
		CheckAndDrawBackground( time, blurWorldModel );
		hadOwnBackground = true;
	} else if( hadOwnBackground ) {
		ResetBackground();
	}

	// Draw items that are intended to be put behind the Chromium buffer
	drawnItemsRegistry.DrawNegativeZItems( time );

	// TODO: Avoid this if the shader has not been invalidated and there were no updates!
	RegisterChromiumBufferShader();

	// TODO: Avoid drawning of UI overlay at all if there is nothing to show!
	// The only reliable way of doing that is adding syscalls...
	vec4_t color = { 1.0f, 1.0f, 1.0f, 1.0f };
	RF_DrawStretchPic( 0, 0, width, height, 0.0f, 0.0f, 1.0f, 1.0f, color, chromiumShader );

	// Draw items that are intended to be put in front of the Chromium buffer
	drawnItemsRegistry.DrawPositiveZItems( time );

	if( showCursor ) {
		int cursorX = parent->mouseXY[0], cursorY = parent->mouseXY[1];
		RF_DrawStretchPic( cursorX, cursorY, 32, 32, 0.0f, 0.0f, 1.0f, 1.0f, color, cursorShader );
	}

	wasRendererDeviceLost = isRendererDeviceLost;
}

void RendererCompositionProxy::CheckAndDrawBackground( int64_t time, bool blurred ) {
	constexpr const char *worldModelName = "maps/ui.bsp";
	if( !hasStartedWorldModelLoading ) {
		RF_RegisterWorldModel( worldModelName );
		hasStartedWorldModelLoading = true;
	} else if( !hasSucceededWorldModelLoading ) {
		if( R_RegisterModel( worldModelName ) ) {
			hasSucceededWorldModelLoading = true;
			SetupDefaultCamera();
		}
	}

	if( hasSucceededWorldModelLoading ) {
		DrawWorldModel( time, blurWorldModel );
	} else {
		// Draw a fullscreen black quad... we are unsure if the renderer clears default framebuffer
		RF_DrawStretchPic( 0, 0, width, height, 0.0f, 0.0f, 1.0f, 1.0f, colorBlack, whiteShader );
	}
}

void RendererCompositionProxy::SetupDefaultCamera() {
	// This is a copy-paste from map_ui.pk3 CSS
	vec3_t origin { 302.0f, -490.0f, 120.0f };
	vec3_t angles { 0, -240, 0 };

	angles[1] += 1.0f;
	CameraAnimFrame frame1( CameraAnimFrame::FromLookAngles( origin, angles, 90.0f, 0 ) );

	angles[1] -= 2.0f;
	origin[0] += 3.0f;
	origin[1] += 3.0f;
	CameraAnimFrame frame2( CameraAnimFrame::FromLookAngles( origin, angles, 95.0f, 30000 ) );
	CameraAnimFrame frame3 = frame1;
	frame3.timestamp = 60000;
	CameraAnimFrame frames[3] = { frame1, frame2, frame3 };
	worldCameraAnimator.ResetWithLoop( frames, frames + 3 );
}

void RendererCompositionProxy::DrawWorldModel( int64_t time, bool blurred ) {
	worldCameraAnimator.Refresh( time );

	refdef_t rdf;
	memset( &rdf, 0, sizeof( rdf ) );
	rdf.areabits = nullptr;
	rdf.x = 0;
	rdf.y = 0;
	rdf.width = width;
	rdf.height = height;

	const CameraAnimParams &params = worldCameraAnimator.GetAnimatedProperty();
	VectorCopy( params.origin, rdf.vieworg );
	Matrix3_Copy( params.axis, rdf.viewaxis );
	rdf.fov_x = params.fov;
	rdf.fov_y = CalcFov( rdf.fov_x, width, height );
	AdjustFov( &rdf.fov_x, &rdf.fov_y, rdf.width, rdf.height, false );
	rdf.time = time;

	rdf.scissor_x = 0;
	rdf.scissor_y = 0;
	rdf.scissor_width = width;
	rdf.scissor_height = height;

	RF_ClearScene();
	RF_RenderScene( &rdf );
	if( blurred ) {
		RF_BlurScreen();
	}
}

RendererCompositionProxy::NativelyDrawnItem::NativelyDrawnItem( RendererCompositionProxy *parent_,
																const ItemDrawParams &drawParams )
	: parent( parent_ ), zIndex( drawParams.ZIndex() ) {
	const float *topLeft = drawParams.TopLeft();
	Vector2Copy( topLeft, this->viewportTopLeft );
	const float *dimensions = drawParams.Dimensions();
	Vector2Copy( dimensions, this->viewportDimensions );
}

RendererCompositionProxy::DrawnAliasModel::DrawnAliasModel( RendererCompositionProxy *parent_,
															const ModelDrawParams &drawParams )
	: NativelyDrawnItem( parent_, drawParams ) {
	const ModelAnimFrame *animBegin = drawParams.AnimFrames().data();
	const ModelAnimFrame *animEnd = animBegin + drawParams.AnimFrames().size();
	if( drawParams.IsAnimLooping() ) {
		animator.ResetWithLoop( animBegin, animEnd );
	} else {
		animator.ResetWithSequence( animBegin, animEnd );
	}

	memset( &entity, 0, sizeof( entity ) );
	const int color = drawParams.ColorRgba();
	Vector4Set( entity.color, COLOR_R( color ), COLOR_G( color ), COLOR_B( color ), COLOR_A( color ) );
	this->modelName = drawParams.Model();
	this->skinName = drawParams.Skin();
}

void RendererCompositionProxy::DrawnAliasModel::DrawSelf( int64_t time ) {
	// TODO: This is a placeholder drawn!!!
	int x = viewportTopLeft[0];
	int y = viewportTopLeft[1];
	int w = viewportDimensions[0];
	int h = viewportDimensions[1];
	vec4_t color = { entity.color[0] / 255.0f, entity.color[1] / 255.0f, entity.color[2] / 255.0f, entity.color[3] / 255.0f };
	RF_DrawStretchPic( x, y, w, h, 0.0, 0.0f, 1.0f, 1.0f, color, R_RegisterPic( "$whiteimage") );
}

RendererCompositionProxy::Drawn2DImage::Drawn2DImage( RendererCompositionProxy *parent_,
													  const ImageDrawParams &drawParams )
	: NativelyDrawnItem( parent_, drawParams ) {
	this->shaderName = drawParams.Shader();
}

void RendererCompositionProxy::Drawn2DImage::DrawSelf( int64_t time ) {
	int x = viewportTopLeft[0];
	int y = viewportTopLeft[1];
	int w = viewportDimensions[0];
	int h = viewportDimensions[1];
	RF_DrawStretchPic( x, y, w, h, 0.0f, 0.0f, 1.0f, 1.0f, colorWhite, R_RegisterPic( shaderName.c_str() ) );
}

void RendererCompositionProxy::OnRendererDeviceAcquired( int newWidth, int newHeight ) {
	wasRendererDeviceLost = isRendererDeviceLost;
	isRendererDeviceLost = false;

	if( width == newWidth && height == newHeight ) {
		return;
	}

	width = newWidth;
	height = newHeight;

	ResizeBuffer();
}