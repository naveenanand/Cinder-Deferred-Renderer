//What is this?
//A Cinder app that utilizes a deferred rendering engine to render lights and SSAO. There is also point-light shadow-mapping (heavy GPU cost though)

//TODO: lots of optimization to do here yet.
//!!!!! If your computer is slow turn off the shadow-mapping by setting last parameter of Light_PS to false (not true)
//Main reason this will be slow will be GPU pipes and RAM as deferred rendering + shadow-mapping uses a huge amount if VRAM (one of its disadvantages)
//Deferred Rendering ADVANTAGE: tons of dynamic point lights (w/o shadows) possible, if not at GPU limits already anyhow

//Inspiration and shader base from http://www.gamedev.net/page/resources/_/technical/graphics-programming-and-theory/image-space-lighting-r2644 
//and http://http.developer.nvidia.com/GPUGems2/gpugems2_chapter09.html

//Tips to get better framerates:
// - adjust ap window size
// - go to initFBO's and lower resolution of maps
// - turn off number shadow-mapped lights by setting last parameter of LIGHT_PS constructor to false
// - Tested on Macbook Pro 2010 Mountain Lion ~30fps
// - Tested on HP620 Windows 7 ~60-70 fps

//Controls
//keys 1-8 toggle through deferred layers (depth, colour, normal, shadows etc.)
//key 9 shows final composed scene
//Using MayaCam so use alt and mouse buttons to move camera
//can move a selected light with arrows keys (and shift and arrow keys for up and down movement)
//switch between "selected" lights by using - and + (will update index number in params with a delay)

//please let me know if you make any optimizations, or have any optimization suggestions. Always more to learn; and more fps is always appreciated :)

#ifndef CinderDeferredRendering_CinderDeferredRenderingApp_h
#define CinderDeferredRendering_CinderDeferredRenderingApp_h

#include "cinder/app/AppBasic.h"
#include "cinder/gl/gl.h"
#include "cinder/Camera.h"
#include "cinder/gl/GlslProg.h"
#include "cinder/gl/Texture.h"
#include "cinder/ImageIo.h"
#include "cinder/MayaCamUI.h"
#include "cinder/params/Params.h"
#include "cinder/Rand.h"

#include "boost/function.hpp"
#include "boost/bind.hpp"

#include "CubeShadowMap.h"
#include "DeferredRenderer.h"

#include "Resources.h"

#include <boost/lambda/lambda.hpp>

#if defined( CINDER_MAC )
#include "AppleUtilities.h"
#endif

using namespace ci;
using namespace ci::app;
using namespace std;

static const float	APP_RES_HORIZONTAL = 1440;
static const float	APP_RES_VERTICAL = 900;
static const Vec3f	CAM_POSITION_INIT( -14.0f, 7.0f, -14.0f );
static const Vec3f	LIGHT_POSITION_INIT( 3.0f, 1.5f, 0.0f );
static const int    NUM_LIGHTS = 500;        //number of lights


class CinderDeferredRenderingApp : public AppBasic 
{
public:
    CinderDeferredRenderingApp();
    virtual ~CinderDeferredRenderingApp();
    void prepareSettings( Settings *settings );
    
    void setup();
    void update();
    void draw();
    
    void mouseDown( MouseEvent event );
	void mouseDrag( MouseEvent event );
    void keyDown( app::KeyEvent event );
    
    void drawShadowCasters(gl::GlslProg* deferShader) const;
    void drawNonShadowCasters(gl::GlslProg* deferShader) const;
    
protected:
    int RENDER_MODE;
	
    //debug
    cinder::params::InterfaceGl mParams;
    bool				mShowParams;
    float				mCurrFramerate;
    gl::Texture         mEarthTex;
	
    //camera
    MayaCamUI           mMayaCam;
    DeferredRenderer    mDeferredRenderer;
    
    int mCurrLightIndex;
};


CinderDeferredRenderingApp::CinderDeferredRenderingApp(){}
CinderDeferredRenderingApp::~CinderDeferredRenderingApp(){}

#pragma mark - lifecycle functions

void CinderDeferredRenderingApp::prepareSettings( Settings *settings )
{
	settings->setWindowSize( APP_RES_HORIZONTAL, APP_RES_VERTICAL );
    settings->setBorderless( true );
	settings->setFrameRate( 1000.0f );			//the more the merrier!
	settings->setResizable( false );			//this isn't going to be resizable
    settings->setFullScreen( false );
    
	//make sure secondary screen isn't blacked out as well when in fullscreen mode ( do wish it could accept keyboard focus though :(
	//settings->enableSecondaryDisplayBlanking( false );
}

void CinderDeferredRenderingApp::setup()
{
    //!!test texture for diffuse texture
    mEarthTex = gl::Texture( loadImage( loadResource( RES_EARTH_TEX ) ) );
    
	gl::disableVerticalSync(); //so I can get a true representation of FPS (if higher than 60 anyhow :/)
    
    #if defined( CINDER_MAC )
    //setAlwaysOnTop(true); //the easy but inconvenient way to have fullscreen but not have app have complete control
    AppleUtilities::autohideMenuBar(); //a better but more complicated way to do the above
    #endif
    
	RENDER_MODE = DeferredRenderer::SHOW_FINAL_VIEW;
    
	mParams = params::InterfaceGl( "3D_Scene_Base", Vec2i( 225, 125 ) );
	mParams.addParam( "Framerate", &mCurrFramerate, "", true );
    mParams.addParam( "Selected Light Index", &mCurrLightIndex);
	mParams.addParam( "Show/Hide Params", &mShowParams, "key=x");
	mParams.addSeparator();
    
	mCurrFramerate = 0.0f;
	mShowParams = true;
	
	//set up camera
    CameraPersp initialCam;
	initialCam.setPerspective( 45.0f, getWindowAspectRatio(), 0.1, 10000 );
    initialCam.lookAt(CAM_POSITION_INIT * 1.5f, Vec3f::zero(), Vec3f(0.0f, 1.0f, 0.0f) );
    initialCam.setCenterOfInterestPoint(Vec3f::zero());
	mMayaCam.setCurrentCam( initialCam );

    //create functions pointers to send to deferred renderer
    boost::function<void(gl::GlslProg*)> fRenderShadowCastersFunc;
    boost::function<void(gl::GlslProg*)> fRenderNotShadowCastersFunc;
    fRenderShadowCastersFunc = boost::bind(&CinderDeferredRenderingApp::drawShadowCasters, this, boost::lambda::_1 );
    fRenderNotShadowCastersFunc = boost::bind(&CinderDeferredRenderingApp::drawNonShadowCasters, this,  boost::lambda::_1 );
    
    //setting up deferred renderer ...
    mDeferredRenderer.setup( fRenderShadowCastersFunc, fRenderNotShadowCastersFunc, &mMayaCam );
    
    //have these cast point light shadows
    mDeferredRenderer.addCubeLight(    Vec3f(-2.0f, 4.0f, 6.0f),      Vec3f(0.10f, 0.69f, 0.93f) * LIGHT_BRIGHTNESS, true);      //blue
    mDeferredRenderer.addCubeLight(    Vec3f(4.0f, 6.0f, -4.0f),      Vec3f(0.94f, 0.15f, 0.23f) * LIGHT_BRIGHTNESS, true);      //red
    
//    mDeferredRenderer.addCubeLight(    Vec3f(-10.0f, 8.0f, 12.0f),    Vec3f(0.99f, 0.67f, 0.23f) * LIGHT_BRIGHTNESS, true);           //orange
//    mDeferredRenderer.addCubeLight(    Vec3f(6.0f, 10.0f, -10.0f),    Vec3f(0.97f, 0.24f, 0.85f) * LIGHT_BRIGHTNESS, true);           //pink
//    mDeferredRenderer.addCubeLight(    Vec3f(-8.0f, 12.0f, -8.0f),    Vec3f(0.00f, 0.93f, 0.30f) * LIGHT_BRIGHTNESS, true);           //green
//    mDeferredRenderer.addCubeLight(    Vec3f(12.0f, 8.0f, 14.0f),     Vec3f(0.98f, 0.96f, 0.32f) * LIGHT_BRIGHTNESS, true);            //yellow
    
    //add a bunch of lights
    for(int i = 0; i < NUM_LIGHTS; i++) {
        
        int randColIndex = Rand::randInt(5);
        Vec3f randCol;
        switch( randColIndex ) {
            case 0:
                randCol = Vec3f(0.99f, 0.67f, 0.23f); //orange
                break;
            case 1:
                randCol = Vec3f(0.97f, 0.24f, 0.85f); //pink
                break;
            case 2:
                randCol = Vec3f(0.00f, 0.93f, 0.30f); //green
                break;
            case 3:
                randCol = Vec3f(0.98f, 0.96f, 0.32f); //yellow
                break;
            case 4:
                randCol = Vec3f(0.10f, 0.69f, 0.93f); //blue
                break;
            case 5:
                randCol = Vec3f(0.94f, 0.15f, 0.23f); //red
                break;
        };
        
        mDeferredRenderer.addCubeLight( Vec3f(Rand::randFloat(-1000.0f, 1000.0f), Rand::randFloat(0.0f, 50.0f), Rand::randFloat(-1000.0f, 1000.0f)),
                                        randCol * LIGHT_BRIGHTNESS);
    }
    
    mCurrLightIndex = 0;
}

void CinderDeferredRenderingApp::update()
{
	mCurrFramerate = getAverageFps();
}

void CinderDeferredRenderingApp::draw()
{
    mDeferredRenderer.renderFullScreenQuad(RENDER_MODE);
    
	if (mShowParams)
		params::InterfaceGl::draw();
}

void CinderDeferredRenderingApp::mouseDown( MouseEvent event )
{
	if( event.isAltDown() )
		mMayaCam.mouseDown( event.getPos() );
}

void CinderDeferredRenderingApp::mouseDrag( MouseEvent event )
{
	if( event.isAltDown() )
		mMayaCam.mouseDrag( event.getPos(), event.isLeftDown(), event.isMiddleDown(), event.isRightDown() );
}

void CinderDeferredRenderingApp::keyDown( KeyEvent event ) 
{
    float lightMovInc = 0.1f;
    
	switch ( event.getCode() ) 
	{
        //switch between render views
		case KeyEvent::KEY_0:
        {RENDER_MODE = DeferredRenderer::SHOW_FINAL_VIEW;}
			break;
		case KeyEvent::KEY_1:
        {RENDER_MODE = DeferredRenderer::SHOW_DIFFUSE_VIEW;}
			break;
		case KeyEvent::KEY_2:
        {RENDER_MODE = DeferredRenderer::SHOW_NORMALMAP_VIEW;}
			break;
		case KeyEvent::KEY_3:
        {RENDER_MODE = DeferredRenderer::SHOW_DEPTH_VIEW;}
			break;
        case KeyEvent::KEY_4:
        {RENDER_MODE = DeferredRenderer::SHOW_POSITION_VIEW;}
			break;
        case KeyEvent::KEY_5:
        {RENDER_MODE = DeferredRenderer::SHOW_ATTRIBUTE_VIEW;}
			break;
        case KeyEvent::KEY_6:
        {RENDER_MODE = DeferredRenderer::SHOW_SSAO_VIEW;}
			break;
        case KeyEvent::KEY_7:
        {RENDER_MODE = DeferredRenderer::SHOW_SSAO_BLURRED_VIEW;}
			break;
        case KeyEvent::KEY_8:
        {RENDER_MODE = DeferredRenderer::SHOW_LIGHT_VIEW;}
            break;
        case KeyEvent::KEY_9:
        {RENDER_MODE = DeferredRenderer::SHOW_SHADOWS_VIEW;}
			break;
            
        //change which cube you want to control
        case 269: {
            //minus key
            if( mDeferredRenderer.getNumCubeLights() > 0) {
                --mCurrLightIndex;
                if ( mCurrLightIndex < 0) mCurrLightIndex = mDeferredRenderer.getNumCubeLights() - 1;
            }
        }
            break;
        case 61: {
            if( mDeferredRenderer.getNumCubeLights() > 0) {
                //plus key
                ++mCurrLightIndex;
                if ( mCurrLightIndex > mDeferredRenderer.getNumCubeLights() - 1) mCurrLightIndex = 0;
            }
        }
            break;
			
        //move selected cube light
		case KeyEvent::KEY_UP: {
            if ( mDeferredRenderer.getNumCubeLights() > 0) {
                if(event.isShiftDown())
                {mDeferredRenderer.getCubeLightsRef()->at(mCurrLightIndex)->setPos( mDeferredRenderer.getCubeLightsRef()->at(mCurrLightIndex)->getPos() + Vec3f(0.0f, lightMovInc, 0.0f ));}
                else
                {mDeferredRenderer.getCubeLightsRef()->at(mCurrLightIndex)->setPos( mDeferredRenderer.getCubeLightsRef()->at(mCurrLightIndex)->getPos() + Vec3f(0.0f, 0.0f, lightMovInc));}
            }
		}
			break;
		case KeyEvent::KEY_DOWN: {
            if ( mDeferredRenderer.getNumCubeLights() > 0) {
                if(event.isShiftDown())
                {mDeferredRenderer.getCubeLightsRef()->at(mCurrLightIndex)->setPos( mDeferredRenderer.getCubeLightsRef()->at(mCurrLightIndex)->getPos() + Vec3f(0.0f, -lightMovInc, 0.0f ));}
                else
                {mDeferredRenderer.getCubeLightsRef()->at(mCurrLightIndex)->setPos( mDeferredRenderer.getCubeLightsRef()->at(mCurrLightIndex)->getPos() + Vec3f(0.0f, 0.0, -lightMovInc));}
            }
		}
			break;
		case KeyEvent::KEY_LEFT: {
            if ( mDeferredRenderer.getNumCubeLights() > 0) {
                mDeferredRenderer.getCubeLightsRef()->at(mCurrLightIndex)->setPos( mDeferredRenderer.getCubeLightsRef()->at(mCurrLightIndex)->getPos() + Vec3f(lightMovInc, 0.0, 0.0f));
            }
		}
			break;
		case KeyEvent::KEY_RIGHT: {
            if ( mDeferredRenderer.getNumCubeLights() > 0) {
                mDeferredRenderer.getCubeLightsRef()->at(mCurrLightIndex)->setPos( mDeferredRenderer.getCubeLightsRef()->at(mCurrLightIndex)->getPos() + Vec3f(-lightMovInc, 0.0, 0.0f));
            }
		}
            break;
        case KeyEvent::KEY_ESCAPE: {
            //never know when you need to quit quick
            exit(1);
        }
			break;
		default:
			break;
	}
}

#pragma mark - render functions

void CinderDeferredRenderingApp::drawShadowCasters(gl::GlslProg* deferShader) const
{
	//just some test objects
    if(deferShader != NULL) {
        deferShader->uniform("useTexture", 1.0f);
        deferShader->uniform("tex0", 0);
        mEarthTex.bind(0);
    }
    
    glColor3ub(255,0,0);
    gl::drawSphere( Vec3f(-1.0, 0.0,-1.0), 1.0f, 30 );
    
    if(deferShader != NULL) {
        deferShader->uniform("useTexture", 0.0f);
        mEarthTex.unbind(0);
    }
    
	glColor3ub(0,255,0);
    gl::drawCube( Vec3f( 1.0f, 0.0f, 1.0f ), Vec3f( 2.0f, 2.0f, 2.0f ) );
    
    glColor3ub(255,0,255);
    gl::drawCube( Vec3f( 0.0f, 0.0f, 4.5f ), Vec3f( 1.0f, 2.0f, 1.0f ) );
    
    glColor3ub(255,255,0);
    gl::drawCube( Vec3f( 3.0f, 0.0f, -1.5f ), Vec3f( 1.0f, 3.0f, 1.0f ) );
    
	glColor3ub(255,0,255);
    gl::pushMatrices();
	glTranslatef(-2.0f, -0.7f, 2.0f);
	glRotated(90.0f, 1, 0, 0);
    gl::drawTorus( 1.0f, 0.3f, 32, 64 );
	gl::popMatrices();
    
}

void CinderDeferredRenderingApp::drawNonShadowCasters(gl::GlslProg* deferShader) const
{
    int size = 3000;
    //a plane to capture shadows (though it won't cast any itself)
    glColor3ub(255, 255, 255);
    glNormal3f(0.0f, 1.0f, 0.0f);
    glBegin(GL_QUADS);
    glVertex3i( size, -1,-size);
    glVertex3i(-size, -1,-size);
    glVertex3i(-size, -1, size);
    glVertex3i( size, -1, size);
    glEnd();
}


CINDER_APP_BASIC( CinderDeferredRenderingApp, RendererGl )

#endif
