/*
 * ===========================================================================
 * Loom SDK
 * Copyright 2011, 2012, 2013
 * The Game Engine Company, LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 * ===========================================================================
 */

// TODO! Asset reloading?

#include "loom/graphics/gfxShader.h"
#include "loom/graphics/gfxQuadRenderer.h"
#include "loom/common/assets/assets.h"

#include <stdlib.h>

lmDefineLogGroup(gGFXShaderLogGroup, "GFXShader", 1, LoomLogInfo);

static const GFX::ShaderProgram *lastBoundShader = nullptr;

lmMap<lmString, lmWptr<GFX::Shader>> GFX::Shader::liveShaders;

// Adds a shader to the shader map. This prevents redundant shader compilation in the future.
void GFX::Shader::addShader(const lmString& name, lmSptr<GFX::Shader> sp)
{
    lmAssert(liveShaders.count(name) == 0, "Shader %s already present in shader list", name.c_str());

    liveShaders[name] = sp;
}

// Looks up the shader by name. If we already have a compiled shader from the same file, return that.
lmSptr<GFX::Shader> GFX::Shader::getShader(const lmString& name)
{
    if (liveShaders.count(name) != 0)
    {
        auto weak = liveShaders[name];

        // Remove from map if it's a dead reference
        if (weak.expired())
        {
            liveShaders.erase(name);
            return nullptr;
        }

        return weak.lock();
    }

    return nullptr;
}

// This only gets called from the destructor of Shader
// It's guaranteed that the reference will be dead
void GFX::Shader::removeShader(const lmString& name)
{
    if (liveShaders.count(name) != 0)
    {
        liveShaders.erase(name);
    }
}

// If _name is empty, the constructor will not compile anything.
// In that case, load() should be called.
GFX::Shader::Shader(const lmString& _name, GLenum _type)
: id(0)
, type(_type)
, name(_name)
{
    lmLog(gGFXShaderLogGroup, "Creating shader %s", name.c_str());

    if (name.size() > 0)
    {
        char* source = getSourceFromAsset();
        loom_asset_subscribe(name.c_str(), &Shader::reloadCallback, this, false);
        load(source);
    }
}

GFX::Shader::~Shader()
{
    lmLog(gGFXShaderLogGroup, "Deleting shader %s", name.c_str());

    if (id != 0)
    {
        GFX::GL_Context* ctx = Graphics::context();
        ctx->glDeleteShader(id);
    }

    if (name.size() > 0)
    {
        loom_asset_unsubscribe(name.c_str(), &Shader::reloadCallback, this);
        Shader::removeShader(name);
    }
}

GLuint GFX::Shader::getId() const
{
    return id;
}

lmString GFX::Shader::getName() const
{
    if (name.size() == 0)
    {
        // Android NDK doesn't know std::to_string
        // Is there no other cross-platform way?
        static char buf[20];
        sprintf(buf, "%d", id);
        return buf;
    }
    else
    {
        return name;
    }
}


bool GFX::Shader::load(const char* source)
{
    lmAssert(id == 0, "Shader already loaded, clean up first");

    GFX::GL_Context* ctx = Graphics::context();

    id = ctx->glCreateShader(type);

    const GLchar *glsource = static_cast<const GLchar*>(source);
    const GLint length = strlen(source);

    ctx->glShaderSource(id, 1, &glsource, &length);
    Graphics::context()->glCompileShader(id);

    if (!validate())
    {
        ctx->glDeleteShader(id);
        id = 0;
        return false;
    }

    return true;
}

bool GFX::Shader::validate()
{
    GFX::GL_Context* ctx = GFX::Graphics::context();

    GLint status;
    ctx->glGetShaderiv(id, GL_COMPILE_STATUS, &status);

    int infoLen;
    ctx->glGetShaderiv(id, GL_INFO_LOG_LENGTH, &infoLen);
    GLchar* info = nullptr;
    if (infoLen > 1)
    {
        info = (GLchar*)lmAlloc(nullptr, infoLen);
        ctx->glGetShaderInfoLog(id, infoLen, nullptr, info);
    }

    auto name_ = getName();
    if (status == GL_TRUE)
    {
        if (info != nullptr)
        {
            lmLogInfo(gGFXShaderLogGroup, "OpenGL shader %s info: %s", name_.c_str(), info);
        }
        else
        {
            lmLogInfo(gGFXShaderLogGroup, "OpenGL shader %s compilation successful", name_.c_str());
        }
    }
    else
    {
        if (info != nullptr)
        {
            lmLogError(gGFXShaderLogGroup, "OpenGL shader %s error: %s", name_.c_str(), info);
        }
        else
        {
            lmLogError(gGFXShaderLogGroup, "OpenGL shader %s error: No additional information provided.", name_.c_str());
        }

        GFX_DEBUG_BREAK

            return false;
    }

    if (info != nullptr)
        lmFree(nullptr, info);

    return true;
}

char* GFX::Shader::getSourceFromAsset()
{
    void * source = loom_asset_lock(name.c_str(), LATText, 1);
    if (source == nullptr)
    {
        lmLogWarn(gGFXShaderLogGroup, "Unable to lock the asset for shader %s", name.c_str());
        return nullptr;
    }
    loom_asset_unlock(name.c_str());

    return static_cast<char*>(source);
}

void GFX::Shader::reload()
{
    GFX::GL_Context* ctx = Graphics::context();
    ctx->glDeleteShader(id);
    id = 0;

    char* source = getSourceFromAsset();
    load(source);

}

void GFX::Shader::reloadCallback(void *payload, const char *name)
{
    Shader* sp = static_cast<Shader*>(payload);
    sp->reload();
}

GFX::ShaderProgram* GFX::ShaderProgram::getDefaultShader()
{
    if (defaultShader == nullptr)
    {
        defaultShader = lmMakeUptr<GFX::DefaultShader>();
    }

    return defaultShader.get();
}

GFX::ShaderProgram::ShaderProgram()
: programId(0)
{

}

GFX::ShaderProgram::~ShaderProgram()
{
    if (programId == 0)
        return;

    GFX::GL_Context* ctx = Graphics::context();

    ctx->glDetachShader(programId, vertexShader->getId());
    ctx->glDetachShader(programId, fragmentShader->getId());

    vertexShader = nullptr;
    fragmentShader = nullptr;

    ctx->glDeleteProgram(programId);
}

bool GFX::ShaderProgram::operator==(const GFX::ShaderProgram& other) const
{
    return programId == other.programId;
}

bool GFX::ShaderProgram::operator!=(const GFX::ShaderProgram& other) const
{
    return programId != other.programId;
}

GLuint GFX::ShaderProgram::getProgramId() const
{
    return programId;
}

void GFX::ShaderProgram::load(const char* vss, const char* fss)
{
    vertexShader = lmMakeSptr<Shader>("", GL_VERTEX_SHADER);
    vertexShader->load(vss);

    fragmentShader = lmMakeSptr<Shader>("", GL_FRAGMENT_SHADER);
    fragmentShader->load(fss);

    link();
}

void GFX::ShaderProgram::loadFromAssets(const char* vertexShaderPath, const char* fragmentShaderPath)
{
    vertexShader = Shader::getShader(vertexShaderPath);
    if (vertexShader == nullptr)
    {
        vertexShader = lmMakeSptr<Shader>(vertexShaderPath, GL_VERTEX_SHADER);
        Shader::addShader(vertexShaderPath, vertexShader);
    }

    fragmentShader = Shader::getShader(fragmentShaderPath);
    if (fragmentShader == nullptr)
    {
        fragmentShader = lmMakeSptr<Shader>(fragmentShaderPath, GL_FRAGMENT_SHADER);
        Shader::addShader(fragmentShaderPath, fragmentShader);
    }

    link();
}

void GFX::ShaderProgram::link()
{
    GFX::GL_Context* ctx = Graphics::context();

    lmAssert(programId == 0, "Shader program already linked, clean up first!");

    programId = ctx->glCreateProgram();

    // Link the program
    ctx->glAttachShader(programId, fragmentShader->getId());
    ctx->glAttachShader(programId, vertexShader->getId());
    ctx->glLinkProgram(programId);

    if (!validate())
    {
        ctx->glDeleteProgram(programId);
        programId = 0;
        return;
    }

    fragmentShaderId = fragmentShader->getId();
    vertexShaderId = vertexShader->getId();

    // Lookup vertex attribute array locations
    posAttribLoc = Graphics::context()->glGetAttribLocation(programId, "a_position");
    posColorLoc = Graphics::context()->glGetAttribLocation(programId, "a_color0");
    posTexCoordLoc = Graphics::context()->glGetAttribLocation(programId, "a_texcoord0");
}


bool GFX::ShaderProgram::validate()
{
    GLint status;
    GFX::Graphics::context()->glGetProgramiv(programId, GL_LINK_STATUS, &status);

    int infoLen;
    GFX::Graphics::context()->glGetProgramiv(programId, GL_INFO_LOG_LENGTH, &infoLen);
    GLchar* info = nullptr;
    if (infoLen > 1)
    {
        info = (GLchar*)lmAlloc(nullptr, infoLen);
        GFX::Graphics::context()->glGetProgramInfoLog(programId, infoLen, nullptr, info);
    }

    if (status == GL_TRUE)
    {
        if (info != nullptr)
        {
            lmLogInfo(gGFXShaderLogGroup, "OpenGL program name %s & %s info: %s", vertexShader->getName().c_str(), fragmentShader->getName().c_str(), info);
        }
        else
        {
            lmLogInfo(gGFXShaderLogGroup, "OpenGL program name %s & %s linking successful", vertexShader->getName().c_str(), fragmentShader->getName().c_str());
        }
    }
    else
    {
        if (info != NULL)
        {
            lmLogError(gGFXShaderLogGroup, "OpenGL program name %s & %s error: %s", vertexShader->getName().c_str(), fragmentShader->getName().c_str(), info);
        }
        else
        {
            lmLogError(gGFXShaderLogGroup, "OpenGL program name %s & %s error: No additional information provided.", vertexShader->getName().c_str(), fragmentShader->getName().c_str());
        }

        GFX_DEBUG_BREAK

        return false;
    }
    if (info != NULL)
        lmFree(NULL, info);

    return true;
}


GLint GFX::ShaderProgram::getUniformLocation(const char* name)
{
    GFX::GL_Context* ctx = Graphics::context();
    return ctx->glGetUniformLocation(programId, name);
}

void GFX::ShaderProgram::setUniform1f(GLint location, GLfloat v0)
{
    lmAssert(this == lastBoundShader, "You are setting a uniform for a shader that is not currently bound!");

    Graphics::context()->glUniform1f(location, v0);
}

int GFX::ShaderProgram::setUniform1fv(lua_State *L)
{
    GLint location = (GLint)lua_tonumber(L, 2);
    int length = lsr_vector_get_length(L, 3);

    utArray<float> values;

    lua_rawgeti(L, 3, LSINDEXVECTOR);
    int vidx = lua_gettop(L);

    for (int i = 0; i < length; i++)
    {
        lua_rawgeti(L, vidx, i);
        float value = (float)lua_tonumber(L, -1);
        values.push_back(value);
    }

    Graphics::context()->glUniform1fv(location, values.size(), values.ptr());
    return 0;
}

void GFX::ShaderProgram::setUniform2f(GLint location, GLfloat v0, GLfloat v1)
{
    lmAssert(this == lastBoundShader, "You are setting a uniform for a shader that is not currently bound!");

    Graphics::context()->glUniform2f(location, v0, v1);
}

int GFX::ShaderProgram::setUniform2fv(lua_State *L)
{
    lmAssert(this == lastBoundShader, "You are setting a uniform for a shader that is not currently bound!");

    GLint location = (GLint)lua_tonumber(L, 2);
    int length = lsr_vector_get_length(L, 3);

    lmAssert(length % 2 == 0, "values size must be a multiple of 2");

    utArray<float> values;

    lua_rawgeti(L, 3, LSINDEXVECTOR);
    int vidx = lua_gettop(L);

    for (int i = 0; i < length; i++)
    {
        lua_rawgeti(L, vidx, i);
        float value = (float)lua_tonumber(L, -1);
        values.push_back(value);
    }

    // Pop the vector
    lua_pop(L, 3);
    // Pop location
    lua_pop(L, 2);

    lmAssert(length % 2 == 0, "values size must be a multiple of 2");

    Graphics::context()->glUniform2fv(location, values.size(), values.ptr());
    return 0;
}

void GFX::ShaderProgram::setUniform3f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2)
{
    lmAssert(this == lastBoundShader, "You are setting a uniform for a shader that is not currently bound!");

    Graphics::context()->glUniform3f(location, v0, v1, v2);
}

int GFX::ShaderProgram::setUniform3fv(lua_State *L)
{
    lmAssert(this == lastBoundShader, "You are setting a uniform for a shader that is not currently bound!");

    GLint location = (GLint)lua_tonumber(L, 2);
    int length = lsr_vector_get_length(L, 3);

    lmAssert(length % 3 == 0, "values size must be a multiple of 3");

    utArray<float> values;

    lua_rawgeti(L, 3, LSINDEXVECTOR);
    int vidx = lua_gettop(L);

    for (int i = 0; i < length; i++)
    {
        lua_rawgeti(L, vidx, i);
        float value = (float)lua_tonumber(L, -1);
        values.push_back(value);
    }

    // Pop the vector
    lua_pop(L, 3);
    // Pop location
    lua_pop(L, 2);

    Graphics::context()->glUniform3fv(location, values.size(), values.ptr());
    return 0;
}

void GFX::ShaderProgram::setUniform1i(GLint location, GLint v0)
{
    lmAssert(this == lastBoundShader, "You are setting a uniform for a shader that is not currently bound!");

    Graphics::context()->glUniform1i(location, v0);
}

int GFX::ShaderProgram::setUniform1iv(lua_State *L)
{
    GLint location = (GLint)lua_tonumber(L, 2);
    int length = lsr_vector_get_length(L, 3);

    utArray<int> values;

    lua_rawgeti(L, 3, LSINDEXVECTOR);
    int vidx = lua_gettop(L);

    for (int i = 0; i < length; i++)
    {
        lua_rawgeti(L, vidx, i);
        int value = (int)lua_tonumber(L, -1);
        values.push_back(value);
    }

    // Pop the vector
    lua_pop(L, 3);
    // Pop location
    lua_pop(L, 2);

    Graphics::context()->glUniform1iv(location, values.size(), values.ptr());
    return 0;
}

void GFX::ShaderProgram::setUniform2i(GLint location, GLint v0, GLint v1)
{
    lmAssert(this == lastBoundShader, "You are setting a uniform for a shader that is not currently bound!");

    Graphics::context()->glUniform2i(location, v0, v1);
}

int GFX::ShaderProgram::setUniform2iv(lua_State *L)
{
    lmAssert(this == lastBoundShader, "You are setting a uniform for a shader that is not currently bound!");

    GLint location = (GLint)lua_tonumber(L, 2);
    int length = lsr_vector_get_length(L, 3);
    lmAssert(length % 2 == 0, "values size must be a multiple of 2");

    utArray<int> values;

    lua_rawgeti(L, 3, LSINDEXVECTOR);
    int vidx = lua_gettop(L);

    for (int i = 0; i < length; i++)
    {
        lua_rawgeti(L, vidx, i);
        int value = (int)lua_tonumber(L, -1);
        values.push_back(value);
    }

    // Pop the vector
    lua_pop(L, 3);
    // Pop location
    lua_pop(L, 2);

    Graphics::context()->glUniform2iv(location, values.size(), values.ptr());
    return 0;
}

void GFX::ShaderProgram::setUniform3i(GLint location, GLint v0, GLint v1, GLint v2)
{
    lmAssert(this == lastBoundShader, "You are setting a uniform for a shader that is not currently bound!");

    Graphics::context()->glUniform3i(location, v0, v1, v2);
}

int GFX::ShaderProgram::setUniform3iv(lua_State *L)
{
    lmAssert(this == lastBoundShader, "You are setting a uniform for a shader that is not currently bound!");

    GLint location = (GLint)lua_tonumber(L, 2);
    int length = lsr_vector_get_length(L, 3);
    lmAssert(length % 3 == 0, "values size must be a multiple of 3");


    utArray<int> values;

    lua_rawgeti(L, 3, LSINDEXVECTOR);
    int vidx = lua_gettop(L);

    for (int i = 0; i < length; i++)
    {
        lua_rawgeti(L, vidx, i);
        int value = (int)lua_tonumber(L, -1);
        values.push_back(value);
    }

    // Pop the vector
    lua_pop(L, 3);
    // Pop location
    lua_pop(L, 2);

    Graphics::context()->glUniform3iv(location, values.size(), values.ptr());
    return 0;
}

void GFX::ShaderProgram::setUniformMatrix3f(GLint location, bool transpose, const Loom2D::Matrix* value)
{
    lmAssert(this == lastBoundShader, "You are setting a uniform for a shader that is not currently bound!");

    static float v[9] = {0, 0, 0, 0, 0, 0, 0, 0 ,0};
    value->copyToMatrix3f(v);
    Graphics::context()->glUniformMatrix3fv(location, 1, transpose, v);
}

int GFX::ShaderProgram::setUniformMatrix3fv(lua_State *L)
{
    lmAssert(this == lastBoundShader, "You are setting a uniform for a shader that is not currently bound!");

    GLint location = (GLint)lua_tonumber(L, 2);
    bool transpose = lua_toboolean(L, 3) != 0;
    int length = lsr_vector_get_length(L, 4);

    lua_rawgeti(L, 4, LSINDEXVECTOR);
    int vidx = lua_gettop(L);

    utArray<float> values;

    values.resize(9 * length);
    for (int i = 0; i < length; i++)
    {
        lua_rawgeti(L, vidx, i);
        Loom2D::Matrix* mat = (Loom2D::Matrix *)lualoom_getnativepointer(L, -1);
        mat->copyToMatrix3f(values.ptr() + i * 9);
    }

    Graphics::context()->glUniformMatrix3fv(location, length, transpose, values.ptr());

    // Pop the vector
    lua_pop(L, 4);
    // Pop transpose
    lua_pop(L, 3);
    // Pop location
    lua_pop(L, 2);

    return 0;
}

void GFX::ShaderProgram::setUniformMatrix4f(GLint location, bool transpose, const Loom2D::Matrix* value)
{
    lmAssert(this == lastBoundShader, "You are setting a uniform for a shader that is not currently bound!");

    static float v[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    value->copyToMatrix4f(v);
    Graphics::context()->glUniformMatrix4fv(location, 1, transpose, v);
}

int GFX::ShaderProgram::setUniformMatrix4fv(lua_State *L)
{
    lmAssert(this == lastBoundShader, "You are setting a uniform for a shader that is not currently bound!");

    GLint location = (GLint)lua_tonumber(L, 2);
    bool transpose = lua_toboolean(L, 3) != 0;
    int length = lsr_vector_get_length(L, 4);

    lua_rawgeti(L, 4, LSINDEXVECTOR);
    int vidx = lua_gettop(L);

    utArray<float> values;

    values.resize(16 * length);
    for (int i = 0; i < length; i++)
    {
        lua_rawgeti(L, vidx, i);
        Loom2D::Matrix* mat = (Loom2D::Matrix *)lualoom_getnativepointer(L, -1);
        mat->copyToMatrix4f(values.ptr() + i * 16);
    }

    Graphics::context()->glUniformMatrix4fv(location, length, transpose, values.ptr());

    // Pop the vector
    lua_pop(L, 4);
    // Pop transpose
    lua_pop(L, 3);
    // Pop location
    lua_pop(L, 2);

    return 0;
}

const Loom2D::Matrix& GFX::ShaderProgram::getMVP() const
{
    return mvp;
}

void GFX::ShaderProgram::setMVP(const Loom2D::Matrix& mat)
{
    mvp = mat;
}

GLuint GFX::ShaderProgram::getTextureId() const
{
    return textureId;
}

void GFX::ShaderProgram::setTextureId(GLuint id)
{
    textureId = id;
}

void GFX::ShaderProgram::bind()
{
    if (programId == 0)
    {
        lmLogError(gGFXShaderLogGroup, "Binding an uninitalized shader!");

        // Don't return here, let it bind to 0
        // It would be wierd if it started using
        // the wrong shader
    }

    lastBoundShader = this;

    GFX::GL_Context* ctx = Graphics::context();

    if (fragmentShaderId != fragmentShader->getId() ||
        vertexShaderId != vertexShader->getId())
    {
        ctx->glDetachShader(programId, fragmentShaderId);
        ctx->glDetachShader(programId, vertexShaderId);
        ctx->glDeleteProgram(programId);
        programId = 0;

        link();
    }

    ctx->glUseProgram(programId);

    if (posAttribLoc != -1)
    {
        ctx->glEnableVertexAttribArray(posAttribLoc);
        ctx->glVertexAttribPointer(posAttribLoc, 3, GL_FLOAT, false,
                                   sizeof(VertexPosColorTex),
                                   (void*)offsetof(VertexPosColorTex, x));
    }

    if (posColorLoc != -1)
    {
        ctx->glEnableVertexAttribArray(posColorLoc);
        ctx->glVertexAttribPointer(posColorLoc, 4, GL_UNSIGNED_BYTE, true,
                                   sizeof(VertexPosColorTex),
                                   (void*)offsetof(VertexPosColorTex, abgr));
    }

    if (posTexCoordLoc != -1)
    {
        ctx->glEnableVertexAttribArray(posTexCoordLoc);
        ctx->glVertexAttribPointer(posTexCoordLoc,
                                   2, GL_FLOAT, false, sizeof(VertexPosColorTex),
                                   (void*)offsetof(VertexPosColorTex, u));
    }

    _onBindDelegate.invoke();
}

const char * defaultVertexShader =
"                                                                    \n"
"attribute vec4 a_position;                                          \n"
"attribute vec4 a_color0;                                            \n"
"attribute vec2 a_texcoord0;                                         \n"
"varying vec2 v_texcoord0;                                           \n"
"varying vec4 v_color0;                                              \n"
"uniform mat4 u_mvp;                                                 \n"
"void main()                                                         \n"
"{                                                                   \n"
"    gl_Position = u_mvp * a_position;                               \n"
"    v_color0 = a_color0;                                            \n"
"    v_texcoord0 = a_texcoord0;                                      \n"
"}                                                                   \n";

const char * defaultFragmentShader =
"                                                                    \n"
#if LOOM_RENDERER_OPENGLES2
"precision mediump float;                                            \n"
#endif
"uniform sampler2D u_texture;                                        \n"
"varying vec2 v_texcoord0;                                           \n"
"varying vec4 v_color0;                                              \n"
"void main()                                                         \n"
"{                                                                   \n"
"    gl_FragColor = v_color0 * texture2D(u_texture, v_texcoord0);    \n"
"}                                                                   \n";

lmUptr<GFX::ShaderProgram> GFX::ShaderProgram::defaultShader = nullptr;

GFX::DefaultShader::DefaultShader()
{
    load(defaultVertexShader, defaultFragmentShader);

    GFX::GL_Context* ctx = Graphics::context();

    uTexture = ctx->glGetUniformLocation(programId, "u_texture");
    uMVP = ctx->glGetUniformLocation(programId, "u_mvp");
}

void GFX::DefaultShader::bind()
{
    GFX::ShaderProgram::bind();

    Graphics::context()->glUniformMatrix4fv(uMVP, 1, GL_FALSE, Graphics::getMVP());
    Graphics::context()->glUniform1i(uTexture, textureId);
}
