/*
  ==============================================================================

    QuatVisualizer.h
    3D orange arrow rendered with OpenGL, rotated by a quaternion.

    Arrow geometry (local space):
      Body:      box   x:±0.08, y:-0.3..+0.3, z:±0.08  (centered at origin)
      Arrowhead: prism base x:±0.2 at y=0.3, tip x=0 at y=0.6, z:±0.08

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

using namespace ::juce::gl;

class QuatVisualizer : public juce::OpenGLAppComponent,
                       public juce::Timer
{
public:
    QuatVisualizer()
    {
        openGLContext.setOpenGLVersionRequired (juce::OpenGLContext::openGL3_2);
        openGLContext.setContinuousRepainting (false);
        setOpaque (true);
        setUpdateFPS (30);
    }

    ~QuatVisualizer() override
    {
        stopTimer();
        shutdownOpenGL();
    }

    void setUpdateFPS (int fps)
    {
        startTimerHz (juce::jlimit (10, 60, fps));
    }

    void timerCallback() override
    {
        openGLContext.triggerRepaint();
    }

    void setQuaternion (float w, float x, float y, float z)
    {
        qw_.store (w); qx_.store (x); qy_.store (y); qz_.store (z);
    }

    void setYawOffset (float degrees)
    {
        yawOffset_.store (degrees * juce::MathConstants<float>::pi / 180.0f);
    }

    //==========================================================================
    void initialise() override
    {
        // ── Shaders ──────────────────────────────────────────────────────────
        const char* vertSrc = R"(
            #version 150
            in vec3 position;
            in vec3 normal;
            out vec3 vNormal;
            uniform mat3 uRotation;
            uniform mat4 uProjection;
            void main()
            {
                vec3 rp = uRotation * position;
                vNormal = uRotation * normal;
                // place model 3 units in front of perspective camera
                gl_Position = uProjection * vec4(rp.x, rp.y, rp.z - 3.0, 1.0);
            }
        )";

        const char* fragSrc = R"(
            #version 150
            in vec3 vNormal;
            out vec4 fragColor;
            uniform vec3 uLightDir;
            void main()
            {
                vec3 orange = vec3(1.0, 0.5, 0.0);
                float diff = max(dot(normalize(vNormal), normalize(uLightDir)), 0.0);
                fragColor = vec4(orange * (0.3 + 0.7 * diff), 1.0);
            }
        )";

        shader_ = std::make_unique<juce::OpenGLShaderProgram> (openGLContext);
        if (! shader_->addVertexShader   (vertSrc)  ||
            ! shader_->addFragmentShader (fragSrc)  ||
            ! shader_->link())
        {
            DBG ("QuatVisualizer shader error: " + shader_->getLastError());
            shader_.reset();
            return;
        }

        uRotation_   = glGetUniformLocation (shader_->getProgramID(), "uRotation");
        uProjection_ = glGetUniformLocation (shader_->getProgramID(), "uProjection");
        uLightDir_   = glGetUniformLocation (shader_->getProgramID(), "uLightDir");
        GLint posLoc  = glGetAttribLocation  (shader_->getProgramID(), "position");
        GLint normLoc = glGetAttribLocation  (shader_->getProgramID(), "normal");

        // ── Axis line shader (position only, flat color) ─────────────────────
        const char* axisVertSrc = R"(
            #version 150
            in vec3 position;
            uniform mat3 uRotation;
            uniform mat4 uProjection;
            void main()
            {
                vec3 rp = uRotation * position;
                gl_Position = uProjection * vec4(rp.x, rp.y, rp.z - 3.0, 1.0);
            }
        )";
        const char* axisFragSrc = R"(
            #version 150
            out vec4 fragColor;
            uniform vec4 uColor;
            void main() { fragColor = uColor; }
        )";

        axisShader_ = std::make_unique<juce::OpenGLShaderProgram> (openGLContext);
        if (! axisShader_->addVertexShader   (axisVertSrc) ||
            ! axisShader_->addFragmentShader (axisFragSrc) ||
            ! axisShader_->link())
        {
            DBG ("QuatVisualizer axis shader error: " + axisShader_->getLastError());
            axisShader_.reset();
        }
        else
        {
            uAxisRotation_   = glGetUniformLocation (axisShader_->getProgramID(), "uRotation");
            uAxisProjection_ = glGetUniformLocation (axisShader_->getProgramID(), "uProjection");
            uAxisColor_      = glGetUniformLocation (axisShader_->getProgramID(), "uColor");
            GLint axPosLoc   = glGetAttribLocation  (axisShader_->getProgramID(), "position");

            // Axis lines: origin → X(0.5,0,0), origin → Y(0,0.5,0), origin → Z(0,0,0.5)
            const float axisVerts[] = {
                0,0,0,  0.5f,0,0,       // X axis
                0,0,0,  0,0.5f,0,       // Y axis
                0,0,0,  0,0,0.5f        // Z axis
            };

            glGenVertexArrays (1, &axisVao_);
            glBindVertexArray (axisVao_);

            glGenBuffers (1, &axisVbo_);
            glBindBuffer (GL_ARRAY_BUFFER, axisVbo_);
            glBufferData (GL_ARRAY_BUFFER, sizeof (axisVerts), axisVerts, GL_STATIC_DRAW);

            if (axPosLoc >= 0)
            {
                glEnableVertexAttribArray ((GLuint)axPosLoc);
                glVertexAttribPointer ((GLuint)axPosLoc, 3, GL_FLOAT, GL_FALSE,
                                       3 * (GLsizei)sizeof (float), (void*)0);
            }

            glBindVertexArray (0);
        }

        // ── Geometry: interleaved [px,py,pz, nx,ny,nz] per vertex ────────────
        std::vector<float>  verts;
        std::vector<GLuint> idx;

        auto addVert = [&](float px, float py, float pz,
                           float nx, float ny, float nz)
        {
            verts.push_back (px); verts.push_back (py); verts.push_back (pz);
            verts.push_back (nx); verts.push_back (ny); verts.push_back (nz);
        };

        // Adds a quad (4 vertices, 2 triangles) with a flat normal
        auto addQuad = [&](float ax, float ay, float az,
                           float bx, float by, float bz,
                           float cx, float cy, float cz,
                           float dx, float dy, float dz,
                           float nx, float ny, float nz)
        {
            auto base = (GLuint)(verts.size() / 6);
            addVert (ax,ay,az, nx,ny,nz);
            addVert (bx,by,bz, nx,ny,nz);
            addVert (cx,cy,cz, nx,ny,nz);
            addVert (dx,dy,dz, nx,ny,nz);
            idx.insert (idx.end(), {base, base+1, base+2,
                                    base, base+2, base+3});
        };

        // Adds a triangle with a flat normal
        auto addTri = [&](float ax, float ay, float az,
                          float bx, float by, float bz,
                          float cx, float cy, float cz,
                          float nx, float ny, float nz)
        {
            auto base = (GLuint)(verts.size() / 6);
            addVert (ax,ay,az, nx,ny,nz);
            addVert (bx,by,bz, nx,ny,nz);
            addVert (cx,cy,cz, nx,ny,nz);
            idx.insert (idx.end(), {base, base+1, base+2});
        };

        // ── Body box ─────────────────────────────────────────────────────────
        const float bx = 0.08f, by = 0.3f, bz = 0.08f;

        addQuad (-bx,-by, bz,  bx,-by, bz,  bx, by, bz,  -bx, by, bz,   0, 0, 1);   // front
        addQuad ( bx,-by,-bz, -bx,-by,-bz, -bx, by,-bz,   bx, by,-bz,   0, 0,-1);   // back
        addQuad ( bx,-by,-bz,  bx,-by, bz,  bx, by, bz,   bx, by,-bz,   1, 0, 0);   // right
        addQuad (-bx,-by, bz, -bx,-by,-bz, -bx, by,-bz,  -bx, by, bz,  -1, 0, 0);  // left
        addQuad (-bx,-by,-bz,  bx,-by,-bz,  bx,-by, bz,  -bx,-by, bz,   0,-1, 0);  // bottom

        // ── Arrowhead prism ───────────────────────────────────────────────────
        // base: x:±0.2 at y=0.3;  tip: x=0 at y=0.6;  z:±0.08
        const float hx = 0.2f, hy0 = 0.3f, hy1 = 0.6f, hz = 0.08f;

        // Slant outward normals: edge (0.2,0.3) → left normal (-0.3,0.2) normalised
        const float slant = std::sqrt (hx*hx + (hy1-hy0)*(hy1-hy0));   // sqrt(0.13)
        const float lnx = -(hy1-hy0) / slant,  lny = hx / slant;       // left face
        const float rnx =  (hy1-hy0) / slant,  rny = hx / slant;       // right face

        addTri  (-hx,hy0, hz,  hx,hy0, hz,  0,hy1, hz,   0, 0, 1);        // front face
        addTri  ( hx,hy0,-hz, -hx,hy0,-hz,  0,hy1,-hz,   0, 0,-1);        // back face
        addQuad (-hx,hy0, hz, -hx,hy0,-hz,  0,hy1,-hz,  0,hy1, hz,  lnx,lny,0); // left slant
        addQuad ( hx,hy0,-hz,  hx,hy0, hz,  0,hy1, hz,  0,hy1,-hz,  rnx,rny,0); // right slant
        addQuad (-hx,hy0,-hz,  hx,hy0,-hz,  hx,hy0, hz, -hx,hy0, hz,  0,-1,0); // base

        numIndices_ = (int)idx.size();

        // ── Upload to GPU ─────────────────────────────────────────────────────
        glGenVertexArrays (1, &vao_);
        glBindVertexArray (vao_);

        glGenBuffers (1, &vbo_);
        glBindBuffer (GL_ARRAY_BUFFER, vbo_);
        glBufferData (GL_ARRAY_BUFFER,
                      (GLsizeiptr)(verts.size() * sizeof (float)),
                      verts.data(), GL_STATIC_DRAW);

        glGenBuffers (1, &ebo_);
        glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, ebo_);
        glBufferData (GL_ELEMENT_ARRAY_BUFFER,
                      (GLsizeiptr)(idx.size() * sizeof (GLuint)),
                      idx.data(), GL_STATIC_DRAW);

        const GLsizei stride = 6 * (GLsizei)sizeof (float);
        if (posLoc >= 0)
        {
            glEnableVertexAttribArray ((GLuint)posLoc);
            glVertexAttribPointer ((GLuint)posLoc, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
        }
        if (normLoc >= 0)
        {
            glEnableVertexAttribArray ((GLuint)normLoc);
            glVertexAttribPointer ((GLuint)normLoc, 3, GL_FLOAT, GL_FALSE, stride,
                                   (void*)(3 * sizeof (float)));
        }

        glBindVertexArray (0);
    }

    //==========================================================================
    void render() override
    {
        glClearColor (0, 0, 0, 1);
        glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        if (shader_ == nullptr) return;

        // Fixed base rotation: X=right, Y=into screen, Z=up
        // Maps model axes to OpenGL space: X→(1,0,0)  Y→(0,0,-1)  Z→(0,1,0)
        // Column-major: col0=(1,0,0), col1=(0,0,-1), col2=(0,1,0)
        static const float kBaseRot[9] = {
            1,  0,  0,    // col 0
            0,  0, -1,    // col 1  (Y → into screen)
            0,  1,  0     // col 2  (Z → up)
        };

        // mat3 multiply: C = A * B (column-major)
        auto mat3Mul = [](const float A[9], const float B[9], float C[9])
        {
            for (int col = 0; col < 3; ++col)
                for (int row = 0; row < 3; ++row)
                {
                    float s = 0;
                    for (int k = 0; k < 3; ++k)
                        s += A[3*k + row] * B[3*col + k];
                    C[3*col + row] = s;
                }
        };

        // Compose: totalQ = mouseQ * sensorQ
        const float sw = qw_.load(), sx = qx_.load(), sy = qy_.load(), sz = qz_.load();
        const float mw = mw_.load(), mx = mx_.load(), my = my_.load(), mz = mz_.load();
        const float w = mw*sw - mx*sx - my*sy - mz*sz;
        const float x = mw*sx + mx*sw + my*sz - mz*sy;
        const float y = mw*sy - mx*sz + my*sw + mz*sx;
        const float z = mw*sz + mx*sy - my*sx + mz*sw;

        // Quaternion → column-major mat3
        const float quatRot[9] = {
            1-2*(y*y+z*z),   2*(x*y+w*z),   2*(x*z-w*y),   // col 0
            2*(x*y-w*z),     1-2*(x*x+z*z), 2*(y*z+w*x),   // col 1
            2*(x*z+w*y),     2*(y*z-w*x),   1-2*(x*x+y*y)  // col 2
        };

        // Yaw offset: rotation around OpenGL Y axis (= world Z / up in the new coord system)
        // kBaseRot maps new-Z to OpenGL-Y, so horizontal-plane rotation = around OpenGL Y
        const float yaw = yawOffset_.load();
        const float cy = std::cos (yaw), sy2 = std::sin (yaw);
        const float yawRot[9] = {
            cy,   0, -sy2,  // col 0
             0,   1,   0,   // col 1
            sy2,  0,   cy   // col 2
        };

        // Arrow rotation = quatRot * yawRot * kBaseRot
        float tmp[9], arrowRot[9];
        mat3Mul (yawRot,  kBaseRot, tmp);
        mat3Mul (quatRot, tmp,      arrowRot);

        // Perspective projection — column-major mat4
        const float aspect = (float)getWidth() / (float)juce::jmax (1, getHeight());
        const float fov    = juce::MathConstants<float>::pi / 4.0f;
        const float f      = 1.0f / std::tan (fov * 0.5f);
        const float zn = 0.1f, zf = 100.0f;
        const float A  = (zf + zn) / (zn - zf);
        const float B  = 2.0f * zf * zn / (zn - zf);
        const float proj[16] = {
            f/aspect, 0,  0,  0,   // col 0
            0,        f,  0,  0,   // col 1
            0,        0,  A, -1,   // col 2
            0,        0,  B,  0    // col 3
        };

        shader_->use();
        glUniformMatrix3fv (uRotation_,   1, GL_FALSE, arrowRot);
        glUniformMatrix4fv (uProjection_, 1, GL_FALSE, proj);
        const float light[3] = { 0.3f, 0.7f, 0.6f };
        glUniform3fv (uLightDir_, 1, light);

        glEnable (GL_DEPTH_TEST);

        // Draw arrow
        glBindVertexArray (vao_);
        glDrawElements (GL_TRIANGLES, numIndices_, GL_UNSIGNED_INT, nullptr);
        glBindVertexArray (0);

        // Draw axes — fixed world frame (kBaseRot only, no quaternion)
        // X=red, Y=green (into screen), Z=blue (up)
        if (axisShader_ != nullptr && axisVao_ != 0)
        {
            axisShader_->use();
            glUniformMatrix3fv (uAxisRotation_,   1, GL_FALSE, kBaseRot);
            glUniformMatrix4fv (uAxisProjection_, 1, GL_FALSE, proj);

            glBindVertexArray (axisVao_);
            const float axes[3][4] = { {1,0,0,1}, {0,1,0,1}, {0,0,1,1} };
            for (int i = 0; i < 3; ++i)
            {
                glUniform4fv (uAxisColor_, 1, axes[i]);
                glDrawArrays (GL_LINES, i * 2, 2);
            }
            glBindVertexArray (0);
        }

        glDisable (GL_DEPTH_TEST);
    }

    //==========================================================================
    void shutdown() override
    {
        shader_.reset();
        axisShader_.reset();
        if (vao_)     { glDeleteVertexArrays (1, &vao_);     vao_     = 0; }
        if (vbo_)     { glDeleteBuffers (1, &vbo_);          vbo_     = 0; }
        if (ebo_)     { glDeleteBuffers (1, &ebo_);          ebo_     = 0; }
        if (axisVao_) { glDeleteVertexArrays (1, &axisVao_); axisVao_ = 0; }
        if (axisVbo_) { glDeleteBuffers (1, &axisVbo_);      axisVbo_ = 0; }
        numIndices_ = 0;
    }

    void paint (juce::Graphics& g) override
    {
        // Project the fixed axis tip positions to screen space and draw labels.
        // Matches the vertex shader: apply kBaseRot, subtract z=3 (camera), perspective divide.
        const float aspect = (float)getWidth() / (float)juce::jmax (1, getHeight());
        const float fov    = juce::MathConstants<float>::pi / 4.0f;
        const float f      = 1.0f / std::tan (fov * 0.5f);

        // kBaseRot * (0.5,0,0), (0,0.5,0), (0,0,0.5)  →  OpenGL positions
        // kBaseRot: X→(1,0,0), Y→(0,0,-1), Z→(0,1,0)
        const float tips[3][3] = {
            { 0.5f,  0.0f,  0.0f },   // X tip after kBaseRot
            { 0.0f,  0.0f, -0.5f },   // Y tip after kBaseRot (into screen)
            { 0.0f,  0.5f,  0.0f }    // Z tip after kBaseRot (up)
        };
        const char*  labels[]  = { "X", "Y", "Z" };
        const juce::Colour colours[] = {
            juce::Colours::red, juce::Colours::green, juce::Colours::blue
        };

        g.setFont (juce::Font (juce::FontOptions().withHeight (13.f).withStyle ("Bold")));

        for (int i = 0; i < 3; ++i)
        {
            const float px = tips[i][0], py = tips[i][1], pz = tips[i][2];
            const float denom = 3.0f - pz;   // -(pz - 3)
            const float ndcX  = (f / aspect) * px / denom;
            const float ndcY  = f * py / denom;

            // NDC → pixel (Y flipped)
            float sx = (ndcX + 1.0f) * 0.5f * (float)getWidth();
            float sy = (1.0f - ndcY) * 0.5f * (float)getHeight();

            // Y axis appears at screen center; nudge it slightly so label is visible
            if (i == 1) { sx += 8.f; sy -= 8.f; }

            g.setColour (colours[i]);
            g.drawText (labels[i],
                        juce::Rectangle<float> (sx + 4.f, sy - 8.f, 16.f, 16.f),
                        juce::Justification::centredLeft);
        }
    }

    //==========================================================================
    void mouseDown (const juce::MouseEvent& e) override
    {
        lastMousePos_ = e.position;
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        const float dx = (float)(e.position.x - lastMousePos_.x);
        const float dy = (float)(e.position.y - lastMousePos_.y);
        lastMousePos_ = e.position;

        const float sensitivity = 0.005f;
        const float angleY = dx * sensitivity;   // drag X → yaw  (rotate around Y)
        const float angleX = dy * sensitivity;   // drag Y → pitch (rotate around X)

        // Incremental quaternion for yaw (Y axis)
        const float sy = std::sin (angleY * 0.5f), cy = std::cos (angleY * 0.5f);
        // Incremental quaternion for pitch (X axis)
        const float sp = std::sin (angleX * 0.5f), cp = std::cos (angleX * 0.5f);

        // Combined: qPitch * qYaw  (applied left-to-right)
        // qYaw  = (cy, 0, sy, 0)  [w,x,y,z]
        // qPitch = (cp, sp, 0, 0)
        // product qPitch * qYaw:
        float nw = cp*cy,  nx = sp*cy,  ny = cp*sy,  nz = -sp*sy;

        // Compose with accumulated mouse rotation: mq = nq * mq
        const float mw = mw_.load(), mx = mx_.load(), my = my_.load(), mz = mz_.load();
        mw_.store (nw*mw - nx*mx - ny*my - nz*mz);
        mx_.store (nw*mx + nx*mw + ny*mz - nz*my);
        my_.store (nw*my - nx*mz + ny*mw + nz*mx);
        mz_.store (nw*mz + nx*my - ny*mx + nz*mw);
    }

private:
    // Sensor quaternion
    std::atomic<float> qw_ {1.0f}, qx_ {0.0f}, qy_ {0.0f}, qz_ {0.0f};
    std::atomic<float> yawOffset_ {0.0f};
    // Mouse-drag accumulated rotation quaternion
    std::atomic<float> mw_ {1.0f}, mx_ {0.0f}, my_ {0.0f}, mz_ {0.0f};

    juce::Point<float> lastMousePos_;

    std::unique_ptr<juce::OpenGLShaderProgram> shader_;
    GLuint vao_ = 0, vbo_ = 0, ebo_ = 0;
    int    numIndices_ = 0;
    GLint  uRotation_ = -1, uProjection_ = -1, uLightDir_ = -1;

    std::unique_ptr<juce::OpenGLShaderProgram> axisShader_;
    GLuint axisVao_ = 0, axisVbo_ = 0;
    GLint  uAxisRotation_ = -1, uAxisProjection_ = -1, uAxisColor_ = -1;
};
