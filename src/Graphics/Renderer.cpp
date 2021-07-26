﻿#include <cmath>
#include <memory>
#define GLM_FORCE_RADIANS
#ifdef __apple_build_version__
    #include <gtc/matrix_transform.hpp>
#else
    #include <glm/gtc/matrix_transform.hpp>
#endif
#include <SDL_image.h>
#include "../Base/Buffer.h"
#include "../CrossPlatform.h"
#include "../Event/State.h"
#include "../Exception.h"
#include "../Game/Game.h"
#include "../Graphics/Point.h"
#include "../Graphics/Renderer.h"
#include "../Graphics/IRendererConfig.h"
#include "../Graphics/Shader.h"
#include "../Graphics/Texture.h"
#include "../Input/Mouse.h"
#include "../ResourceManager.h"
#include "../Settings.h"
#include "../State/State.h"

namespace Falltergeist
{
    namespace Graphics
    {
        using Game::Game;

        Renderer::Renderer(std::unique_ptr<IRendererConfig> rendererConfig, std::shared_ptr<ILogger> logger)
        {
            _rendererConfig = std::move(rendererConfig);
            this->logger = std::move(logger);

            std::string message = "Renderer initialization - ";
            if (SDL_InitSubSystem(SDL_INIT_VIDEO) < 0)
            {
                logger->critical() << "[VIDEO] " << message + "[FAIL]" << std::endl;
                throw Exception(SDL_GetError());
            }
            this->logger->info() << "[VIDEO] " << message << "[OK]" << std::endl;
        }

        Renderer::~Renderer()
        {
            GL_CHECK(glDeleteBuffers(1, &_coord_vbo));
            GL_CHECK(glDeleteBuffers(1, &_texcoord_vbo));
            GL_CHECK(glDeleteBuffers(1, &_ebo));

            if (_renderpath == RenderPath::OGL32)
            {
                GL_CHECK(glDeleteVertexArrays(1, &_vao));
            }
        }

        void Renderer::init()
        {
            // TODO: android/ios
            // _width = deviceWidth;
            // _height = deviceHeight;
            // Game::getInstance()->engineSettings()->setFullscreen(true);
            // Game::getInstance()->engineSettings()->setScale(1); //or 2, if fullhd device

            std::string message =  "SDL_CreateWindow " + std::to_string(_rendererConfig->width()) + "x" + std::to_string(_rendererConfig->height()) + "x" + std::to_string(32) + " - ";

            uint32_t flags = SDL_WINDOW_SHOWN | SDL_WINDOW_OPENGL;

            if (_rendererConfig->isFullscreen()) {
                flags |= SDL_WINDOW_FULLSCREEN;
            }

            if (_rendererConfig->isAlwaysOnTop()) {
                // SDL_WINDOW_ALWAYS_ON_TOP is available on X11 only, >= SDL 2.0.5
                flags |= 0x00008000; // Copied from SDL_WindowFlags::SDL_WINDOW_ALWAYS_ON_TOP
            }

            _sdlWindow = SDL_CreateWindow(
                "",
                _rendererConfig->x(),
                _rendererConfig->y(),
                _rendererConfig->width(),
                _rendererConfig->height(),
                flags
            );

            if (!_sdlWindow) {
                throw Exception(message + "[FAIL]");
            }

            logger->info() << "[RENDERER] " << message + "[OK]" << std::endl;

            message =  "Init OpenGL - ";
            // specifically request 3.2, because fucking Mesa ignores core flag with version < 3
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
            SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
            SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
            _glcontext = SDL_GL_CreateContext(_sdlWindow);

            if (!_glcontext) {
                // ok, try and create 2.1 context then
                SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, 0);
                SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
                SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
                _glcontext = SDL_GL_CreateContext(_sdlWindow);

                if (!_glcontext) {
                    throw Exception(message + "[FAIL]");
                }
            }

            logger->info() << "[RENDERER] " << message + "[OK]" << std::endl;
            SDL_GL_SetSwapInterval(0);

        /*
         * TODO: newrender
            if (Game::getInstance()->settings()->scale() != 0)
            {
                switch (Game::getInstance()->settings()->scale())
                {
                    default:
                    case 1:
                        _size.setWidth((int)(_size.width() / (_size.height() / 480.0)));
                        _size.setHeight(480);
                        break;
                    case 2:
                        _size.setWidth((int)(_size.width() / (_size.height() / 960.0)));
                        _size.setHeight(960);
                        break;
                }
                SDL_RenderSetLogicalSize(_sdlRenderer, _size.width(), _size.height());
                SDL_RenderGetScale(_sdlRenderer, &_scaleX, &_scaleY);
            }
        */

            char *version_string = (char*)glGetString(GL_VERSION);
            if (version_string[0]-'0' >=3) { //we have at least gl 3.0
                glGetIntegerv(GL_MAJOR_VERSION, &_major);
                glGetIntegerv(GL_MINOR_VERSION, &_minor);
                if (_major == 3 && _minor < 2) { // anything lower 3.2
                    _renderpath = RenderPath::OGL21;
                } else {
                    _renderpath = RenderPath::OGL32;
                }
            } else {
                _major = version_string[0]-'0';
                _minor = version_string[2]-'0';
                _renderpath = RenderPath::OGL21;
            }


            logger->info() << "[RENDERER] " << "Using OpenGL " << _major << "." << _minor << std::endl;
            logger->info() << "[RENDERER] " << "Renderer: " << glGetString(GL_RENDERER) << std::endl;
            logger->info() << "[RENDERER] " << "Version string: " << glGetString(GL_VERSION) << std::endl;
            logger->info() << "[RENDERER] " << "Vendor: " << glGetString(GL_VENDOR) << std::endl;
            switch (_renderpath) {
                case RenderPath::OGL21:
                    logger->info() << "[RENDERER] " << "Render path: OpenGL 2.1"  << std::endl;
                    break;
                case RenderPath::OGL32:
                    logger->info() << "[RENDERER] " << "Render path: OpenGL 3.0+" << std::endl;
                    break;
                default:
                    break;
            }

            glGetIntegerv(GL_MAX_TEXTURE_SIZE, &_maxTexSize);

            message =  "Init GLEW - ";
            glewExperimental = GL_TRUE;
            GLenum glewError = glewInit();
            glGetError(); // glew sometimes throws bad enum, so clean it

            switch (glewError) {
                #ifdef GLEW_ERROR_NO_GLX_DISPLAY
                // Wayland: https://github.com/nigels-com/glew/issues/172
                case GLEW_ERROR_NO_GLX_DISPLAY:
                    break;
                #endif
                case GLEW_OK:
                    break;
                default:
                    throw Exception(message + "[FAIL]: " + std::string((char*)glewGetErrorString(glewError)));
            }

            logger->info() << "[RENDERER] " << message + "[OK]" << std::endl;
            logger->info() << "[RENDERER] " << "Using GLEW " << glewGetString(GLEW_VERSION) << std::endl;

            logger->info() << "[RENDERER] " << "Extensions: " << std::endl;

            if (_renderpath == RenderPath::OGL32) {
                GLint count;
                glGetIntegerv(GL_NUM_EXTENSIONS, &count);

                for (GLint i = 0; i < count; i++) {
                    logger->info() << "[RENDERER] " << (const char *) glGetStringi(GL_EXTENSIONS, i) << std::endl;
                }
            } else {
                logger->info() << "[RENDERER] " << (const char *) glGetString(GL_EXTENSIONS) << std::endl;
            }

            logger->info() << "[RENDERER] " << "Loading default shaders" << std::endl;
            ResourceManager::getInstance()->shader("default");
            ResourceManager::getInstance()->shader("sprite");
            ResourceManager::getInstance()->shader("font");
            ResourceManager::getInstance()->shader("animation");
            ResourceManager::getInstance()->shader("tilemap");
            ResourceManager::getInstance()->shader("lightmap");
            logger->info() << "[RENDERER] " << "[OK]" << std::endl;

            logger->info() << "[RENDERER] " << "Generating buffers" << std::endl;

            if (_renderpath == RenderPath::OGL32) {
                // generate VBOs for verts and tex
                GL_CHECK(glGenVertexArrays(1, &_vao));
                GL_CHECK(glBindVertexArray(_vao));
            }

            GL_CHECK(glGenBuffers(1, &_coord_vbo));
            GL_CHECK(glGenBuffers(1, &_texcoord_vbo));

            // pre-populate element buffer. 6 elements, because we draw as triangles
            GL_CHECK(glGenBuffers(1, &_ebo));
            GL_CHECK(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _ebo));
            GLushort indexes[6] = { 0, 1, 2, 3, 2, 1 };
            GL_CHECK(glBufferData(GL_ELEMENT_ARRAY_BUFFER, 6*sizeof(GLushort), indexes, GL_STATIC_DRAW));

            // generate projection matrix
            _MVP = glm::ortho(
                0.0,
                static_cast<double>(_rendererConfig->width()),
                static_cast<double>(_rendererConfig->height()),
                0.0,
                -1.0,
                1.0
            );

            // load egg
            _egg = ResourceManager::getInstance()->texture("data/egg.png");
        }

        void Renderer::think(const float &deltaTime)
        {
            if (_fadeDone) {
                return;
            }

            _fadeTimer += deltaTime;

            float fadeDelay = static_cast<float>(_fadeDelay);
            if (fadeDelay < 1.0f) {
                fadeDelay = 1.0f;
            }

            while (_fadeTimer >= fadeDelay) {
                _fadeTimer -= fadeDelay;
                _fadeAlpha += _fadeStep;
                if (_fadeAlpha <= 0 || _fadeAlpha > 255)
                {
                    _fadeAlpha = (_fadeAlpha <= 0 ? 0 : 255);
                    _fadeDone = true;

                    auto state = Game::getInstance()->topState();
                    state->emitEvent(std::make_unique<Event::State>("fadedone"), state->fadeDoneHandler());
                    return;
                }
            }
            _fadeColor.a = _fadeAlpha;
        }

        bool Renderer::fading()
        {
            return !_fadeDone && !_inmovie;
        }

        void Renderer::fadeIn(uint8_t r, uint8_t g, uint8_t b, unsigned int time, bool inmovie)
        {
            _inmovie = inmovie;
            _fadeColor = {r, g, b, 255};
            _fadeAlpha = 255;
            _fadeStep = -1;
            _fadeDone = false;
            _fadeDelay = static_cast<unsigned>(round(time / 256));
            _fadeTimer = 0;
        }

        void Renderer::fadeOut(uint8_t r, uint8_t g, uint8_t b, unsigned int time, bool inmovie)
        {
            _inmovie = inmovie;
            _fadeColor = {r, g, b, 0};
            _fadeAlpha = 0;
            _fadeStep = 1;
            _fadeDone = false;
            _fadeDelay = static_cast<unsigned>(round(time / 256));
            _fadeTimer = 0;
        }


        void Renderer::beginFrame()
        {
            GL_CHECK(glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));
            GL_CHECK(glEnable(GL_BLEND));
            GL_CHECK(glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));
        }

        void Renderer::endFrame()
        {
            GL_CHECK(glDisable(GL_BLEND));
            SDL_GL_SwapWindow(_sdlWindow);
        }

        unsigned int Renderer::width() const
        {
            return _rendererConfig->width();
        }

        unsigned int Renderer::height() const
        {
            return _rendererConfig->height();
        }

        Size Renderer::size() const
        {
            return {
                static_cast<int>(_rendererConfig->width()),
                static_cast<int>(_rendererConfig->height())
            };
        }

        void Renderer::screenshot()
        {
            std::string filename;
            Uint32 rmask, gmask, bmask, amask;
            SDL_Surface* output;

            int iter = 0;
            do
            {
                std::string siter = std::to_string(iter);
                if(siter.size() < 3)
                {
                    siter.insert(0, 3 - siter.size(), '0');
                }
                filename = "screenshot" + siter + ".png";
                iter++;
            }
            while (CrossPlatform::fileExists(filename) && iter < 1000);

            if (CrossPlatform::fileExists(filename))
            {
                logger->warning() << "[RENDERER] Too many screenshots" << std::endl;
                return;
            }


        #if SDL_BYTEORDER == SDL_BIG_ENDIAN
            rmask = 0xff000000;
            gmask = 0x00ff0000;
            bmask = 0x0000ff00;
            amask = 0x000000ff;
        #else
            rmask = 0x000000ff;
            gmask = 0x0000ff00;
            bmask = 0x00ff0000;
            amask = 0xff000000;
        #endif

            output = SDL_CreateRGBSurface(0, width(), height(), 32, rmask, gmask, bmask, amask);
            uint8_t *destPixels = (uint8_t*)output->pixels;
            Base::Buffer<uint8_t> srcPixels(width() * height() * 4);

            glReadBuffer(GL_BACK);
            glReadPixels(0, 0, width(), height(), GL_RGBA, GL_UNSIGNED_BYTE, srcPixels.data());

            for (int y = 0; y < static_cast<int>(height()); ++y)
            {
                for (int x = 0; x < static_cast<int>(width()); ++x)
                {
                    uint8_t* pDestPix = &destPixels[((width() * y) + x) * 4];
                    uint8_t* pSrcPix = &srcPixels[((width() * ((height() - 1) - y)) + x) * 4];
                    pDestPix[0] = pSrcPix[0];
                    pDestPix[1] = pSrcPix[1];
                    pDestPix[2] = pSrcPix[2];
                    pDestPix[3] = 255;
                }
            }

            IMG_SavePNG(output,filename.c_str());
            SDL_FreeSurface(output);
            logger->info() << "[RENDERER] Screenshot saved to " + filename << std::endl;

            return;

        }

        void Renderer::setCaption(const std::string& caption)
        {
            SDL_SetWindowTitle(_sdlWindow, caption.c_str());
        }

        SDL_Window* Renderer::sdlWindow()
        {
            return _sdlWindow;
        }

        float Renderer::scaleX()
        {
            return _scaleX;
        }

        float Renderer::scaleY()
        {
            return _scaleY;
        }

        GLuint Renderer::getVAO()
        {
            return _vao;
        }

        GLuint Renderer::getVVBO()
        {
            return _coord_vbo;
        }

        GLuint Renderer::getTVBO()
        {
            return _texcoord_vbo;
        }

        glm::mat4 Renderer::getMVP()
        {
            return _MVP;
        }

        GLuint Renderer::getEBO()
        {
            return _ebo;
        }

        void Renderer::drawRect(int x, int y, int w, int h, SDL_Color color)
        {
            std::vector<glm::vec2> vertices;

            glm::vec4 fcolor = glm::vec4((float)color.r/255.0f, (float)color.g/255.0f, (float)color.b/255.0f, (float)color.a/255.0f);

            vertices.push_back(glm::vec2((float)x, (float)y));
            vertices.push_back(glm::vec2((float)x, (float)y+(float)h));
            vertices.push_back(glm::vec2((float)x+(float)w, (float)y));
            vertices.push_back(glm::vec2((float)x+(float)w, (float)y+(float)h));


            GL_CHECK(ResourceManager::getInstance()->shader("default")->use());

            GL_CHECK(ResourceManager::getInstance()->shader("default")->setUniform("color", fcolor));

            GL_CHECK(ResourceManager::getInstance()->shader("default")->setUniform("MVP", getMVP()));

            if (_renderpath==RenderPath::OGL32)
            {
                GLint curvao;
                glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &curvao);
                GLint vao = getVAO();
                if (curvao != vao)
                {
                    GL_CHECK(glBindVertexArray(vao));
                }
            }

            GL_CHECK(glBindBuffer(GL_ARRAY_BUFFER, Game::getInstance()->renderer()->getVVBO()));

            GL_CHECK(glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(glm::vec2), &vertices[0], GL_DYNAMIC_DRAW));

            GL_CHECK(glVertexAttribPointer(ResourceManager::getInstance()->shader("default")->getAttrib("Position"), 2, GL_FLOAT, GL_FALSE, 0, (void*)0 ));

            GL_CHECK(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, Game::getInstance()->renderer()->getEBO()));

            GL_CHECK(glEnableVertexAttribArray(ResourceManager::getInstance()->shader("default")->getAttrib("Position")));

            GL_CHECK(glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0 ));

            GL_CHECK(glDisableVertexAttribArray(ResourceManager::getInstance()->shader("default")->getAttrib("Position")));

        }

        void Renderer::drawRect(const Point &pos, const Size &size, SDL_Color color)
        {
            drawRect(pos.x(), pos.y(), size.width(), size.height(), color);
        }

        glm::vec4 Renderer::fadeColor()
        {
            return glm::vec4((float)_fadeColor.r/255.0, (float)_fadeColor.g/255.0, (float)_fadeColor.b/255.0, (float)_fadeColor.a/255.0);
        }

        int32_t Renderer::maxTextureSize()
        {
            return 1024;
            return _maxTexSize;
        }

        Texture *Renderer::egg()
        {
            return _egg;
        }

        Renderer::RenderPath Renderer::renderPath()
        {
            return _renderpath;
        }
    }
}
