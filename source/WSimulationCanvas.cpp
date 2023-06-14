#include "WSimulationCanvas.h"
#include "ImageEncoder.h"

#include <Wt/WApplication.h>
#include <Wt/WMemoryResource.h>
#include <Wt/WImage.h>
#include <SceneGraph.h>

#include <Module/CalculateNorm.h>
#include <ColorMapping.h>

#include <GLFW/glfw3.h>

#include <GLRenderEngine.h>
#include <GLPointVisualModule.h>
#include <GLSurfaceVisualModule.h>

#include <OrbitCamera.h>
#include <TrackballCamera.h>

using namespace dyno;

WSimulationCanvas::WSimulationCanvas()
{
	this->setLayoutSizeAware(true);
	this->setStyleClass("remote-framebuffer");
	this->resize("100%", "100%");

	this->mouseWentUp().preventDefaultAction(true);
	this->mouseWentDown().preventDefaultAction(true);
	this->mouseDragged().preventDefaultAction(true);
	this->touchStarted().preventDefaultAction(true);
	this->touchMoved().preventDefaultAction(true);
	this->touchEnded().preventDefaultAction(true);

	this->mouseWentDown().connect(this, &WSimulationCanvas::onMousePressed);
	this->mouseWheel().connect(this, &WSimulationCanvas::onMouseWheeled);
	this->mouseDragged().connect(this, &WSimulationCanvas::onMouseDrag);
	this->mouseWentUp().connect(this, &WSimulationCanvas::onMouseReleased);

	this->setAttributeValue("oncontextmenu", "return false;");

	mApp = Wt::WApplication::instance();
	
	mImage = this->addNew<Wt::WImage>();
	mImage->resize("100%", "100%");

	mImage->setJavaScriptMember("currURL", "null");
	mImage->setJavaScriptMember("nextURL", "null");
	mImage->setJavaScriptMember("onload",
		"function() {"
		"this.currURL = this.nextURL;"
		"this.nextURL = null;"
		"if (this.currURL != null) {"
		"this.src = this.currURL;"
		"}"
		"}.bind(" + mImage->jsRef() + ")");
	mImage->setJavaScriptMember("onerror",
		"function() {"
		"this.currURL = this.nextURL;"
		"this.nextURL = null;"
		"if (this.currURL != null) {"
		"this.src = this.currURL;"
		"}"
		"}.bind(" + mImage->jsRef() + ")");
	mImage->setJavaScriptMember("update",
		"function(url) { "
		"if (this.currURL == null) {"
		"this.currURL = url;"
		"this.src = this.currURL;"
		"} else {"						// still loading
		"this.nextURL = url;"
		"}"
		"}.bind(" + mImage->jsRef() + ")");

	
	mImageData.resize(640 * 480 * 3);	// initialize image buffer
	mJpegEncoder = std::make_unique<ImageEncoderNV>();
	mJpegEncoder->SetQuality(90);
	mJpegResource = std::make_unique<Wt::WMemoryResource>("image/jpeg");

	// create an empty scene
	mScene = std::make_shared<dyno::SceneGraph>();

	mRenderEngine = new dyno::GLRenderEngine;
	mRenderParams = new dyno::RenderParams;
	mCamera = std::make_shared<dyno::OrbitCamera>();
	mCamera->setWidth(640);
	mCamera->setHeight(480);
	mCamera->registerPoint(0, 0);
	mCamera->rotateToPoint(-32, 12);

	// initialize OpenGL context and RenderEngine
	this->initializeGL();
}

WSimulationCanvas::~WSimulationCanvas()
{
	makeCurrent();

	mRenderEngine->terminate();
	delete mRenderEngine;

	glfwDestroyWindow(mContext);
}

void WSimulationCanvas::initializeGL()
{
	// initialize render engine and target
	glfwInit();
	// Set all the required options for GLFW    
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

	mContext = glfwCreateWindow(640, 480, "", NULL, NULL);
	if (!mContext)
	{
		Wt::log("error") << "Failed to create OpenGL context!";
		exit(-1);
	}

	makeCurrent();

	mRenderEngine->initialize();

	// create framebuffer here...
	frameColor.format = GL_RGB;
	frameColor.internalFormat = GL_RGB;
	frameColor.type = GL_BYTE;
	frameColor.create();
	frameColor.resize(640, 480);

	framebuffer.create();
	framebuffer.bind();
	const unsigned int GL_COLOR_ATTACHMENT0 = 0x8CE0;
	framebuffer.setTexture2D(GL_COLOR_ATTACHMENT0, frameColor.id);	// 
	unsigned int buffers[]{ GL_COLOR_ATTACHMENT0 };
	framebuffer.drawBuffers(1, buffers);
	framebuffer.unbind();

	doneCurrent();
}

void WSimulationCanvas::makeCurrent()
{
	if(glfwGetCurrentContext() != mContext)
		glfwMakeContextCurrent(mContext);
}

void WSimulationCanvas::doneCurrent()
{
	glfwMakeContextCurrent(NULL);
}

void WSimulationCanvas::layoutSizeChanged(int width, int height)
{
	mCamera->setWidth(width);
	mCamera->setHeight(height);
	mImageData.resize(width * height * 3);

	this->makeCurrent();
	// resize framebuffer
	frameColor.resize(width, height);
	this->doneCurrent();

	WContainerWidget::layoutSizeChanged(width, height); 
	scheduleRender();
}

void WSimulationCanvas::onMousePressed(const Wt::WMouseEvent& evt)
{
	Wt::Coordinates coord = evt.widget();
	mCamera->registerPoint(coord.x, coord.y);
	
}

void WSimulationCanvas::onMouseDrag(const Wt::WMouseEvent& evt)
{
	Wt::Coordinates coord = evt.widget();

	if (evt.button() == Wt::MouseButton::Left) {
		mCamera->rotateToPoint(coord.x, coord.y);
	}
	else if (evt.button() == Wt::MouseButton::Middle) {
		mCamera->translateToPoint(coord.x, coord.y);
	}
	scheduleRender();
}

void WSimulationCanvas::onMouseReleased(const Wt::WMouseEvent& evt)
{

}

void WSimulationCanvas::onMouseWheeled(const Wt::WMouseEvent& evt)
{
	mCamera->zoom(-1.0 * evt.wheelDelta());
	scheduleRender();
}

void WSimulationCanvas::render(Wt::WFlags<Wt::RenderFlag> flags)
{
	update();
}

void WSimulationCanvas::update()
{
	// do render
	{
		this->makeCurrent();

		if (mScene)
		{
			mScene->updateGraphicsContext();
		}

		framebuffer.bind();
		mRenderEngine->draw(mScene.get(), mCamera.get(), *mRenderParams);

		// dump framebuffer
		frameColor.dump(mImageData.data());
		framebuffer.unbind();

		this->doneCurrent();
	}

	// encode image
	{
		mJpegEncoder->Encode(mImageData.data(),
			mCamera->viewportWidth(), mCamera->viewportHeight(), 0,
			mJpegBuffer);

		Wt::log("info") << mCamera->viewportWidth() << " x " << mCamera->viewportHeight()
			<< ", JPG size: " << mJpegBuffer.size() / 1024 << " kb";
	}

	// update UI
	{
		mJpegResource->setData(mJpegBuffer);
		const std::string url = mJpegResource->generateUrl();
		mImage->callJavaScriptMember("update", WWebWidget::jsStringLiteral(url));
	}
}

void WSimulationCanvas::setScene(std::shared_ptr<dyno::SceneGraph> scene)
{
	this->mScene = scene;

	// TODO: move to somewhere else!
	if (this->mScene)
	{
		makeCurrent();
		this->mScene->initialize();
		doneCurrent();

		scheduleRender();
	}

}

dyno::RenderParams* WSimulationCanvas::getRenderParams()
{
	return mRenderParams;
};
