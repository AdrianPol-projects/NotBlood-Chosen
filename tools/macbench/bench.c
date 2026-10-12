// Polymost-like streaming benchmark for macOS OpenGL (legacy 2.1 context, offscreen FBO).
// Each frame: N small polygons, each with its own texture bind + uniform, vertices streamed
// the way polymost does it. Prints CPU submit time and total time (with glFinish) per frame.
#define GL_SILENCE_DEPRECATION
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <mach/mach_time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static double now_ms(void)
{
    static mach_timebase_info_data_t tb;
    if (!tb.denom) mach_timebase_info(&tb);
    return (double)mach_absolute_time() * tb.numer / tb.denom / 1e6;
}

enum { W = 1920, H = 1080, NTEX = 32, BUFVERTS = 30000, FRAMES = 240 };
static float verts[BUFVERTS * 5];
static GLuint prog, uColor, vbo, tex[NTEX];

static GLuint mkshader(GLenum t, const char *src)
{
    GLuint s = glCreateShader(t);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[1000]; glGetShaderInfoLog(s, 1000, NULL, log); printf("shader: %s\n", log); exit(1); }
    return s;
}

static int fillpoly(float *v, int i, int n)
{
    float cx = (float)((i * 37) % 100) / 50.f - 1.f, cy = (float)((i * 61) % 100) / 50.f - 1.f;
    for (int k = 0; k < n; k++)
    {
        float a = k * 6.2831853f / n;
        v[k*5+0] = cx + 0.08f * __builtin_cosf(a);
        v[k*5+1] = cy + 0.08f * __builtin_sinf(a);
        v[k*5+2] = 0.f;
        v[k*5+3] = __builtin_cosf(a);
        v[k*5+4] = __builtin_sinf(a);
    }
    return n;
}

// mode 0: polymost today (one VBO, glBufferSubData per polygon appended, orphan when full)
// mode 1: client-side vertex arrays (no VBO)
// mode 2: glBufferData per polygon (orphan every draw)
// mode 3: reference: whole frame uploaded once, then N draws
static void frame(int mode, int npolys)
{
    static int offset = 0;
    glClear(GL_COLOR_BUFFER_BIT);
    if (mode == 3)
    {
        int total = 0;
        for (int i = 0; i < npolys; i++) total += fillpoly(verts + total * 5, i, 4 + (i % 5));
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, total * 20, verts, GL_STREAM_DRAW);
        glVertexPointer(3, GL_FLOAT, 20, 0);
        glTexCoordPointer(2, GL_FLOAT, 20, (void *)12);
        int first = 0;
        for (int i = 0; i < npolys; i++)
        {
            int n = 4 + (i % 5);
            glBindTexture(GL_TEXTURE_2D, tex[i % NTEX]);
            glUniform4f(uColor, (i % 7) / 7.f, 0.5f, 0.5f, 1.f);
            glDrawArrays(GL_TRIANGLE_FAN, first, n);
            first += n;
        }
        return;
    }
    for (int i = 0; i < npolys; i++)
    {
        int n = fillpoly(verts, i, 4 + (i % 5));
        glBindTexture(GL_TEXTURE_2D, tex[i % NTEX]);
        glUniform4f(uColor, (i % 7) / 7.f, 0.5f, 0.5f, 1.f);
        if (mode == 0)
        {
            if (offset + n > BUFVERTS)
            {
                glBufferData(GL_ARRAY_BUFFER, BUFVERTS * 20, NULL, GL_STREAM_DRAW);
                offset = 0;
            }
            glBufferSubData(GL_ARRAY_BUFFER, offset * 20, n * 20, verts);
            glDrawArrays(GL_TRIANGLE_FAN, offset, n);
            offset += n;
        }
        else if (mode == 1)
        {
            glDrawArrays(GL_TRIANGLE_FAN, 0, n);
        }
        else
        {
            glBufferData(GL_ARRAY_BUFFER, n * 20, verts, GL_STREAM_DRAW);
            glDrawArrays(GL_TRIANGLE_FAN, 0, n);
        }
    }
}

int main(int argc, char **argv)
{
    int npolys = argc > 1 ? atoi(argv[1]) : 400;
    CGLPixelFormatAttribute attrs[] = { kCGLPFAAllowOfflineRenderers, kCGLPFAOpenGLProfile, (CGLPixelFormatAttribute)kCGLOGLPVersion_Legacy,
                                        kCGLPFAColorSize, (CGLPixelFormatAttribute)24, (CGLPixelFormatAttribute)0 };
    CGLPixelFormatObj pf = NULL; GLint npf = 0;
    CGLError err = CGLChoosePixelFormat(attrs, &pf, &npf);
    if (err || !pf) { printf("no pixel format: %s\n", CGLErrorString(err)); return 1; }
    CGLContextObj ctx;
    if (CGLCreateContext(pf, NULL, &ctx)) { printf("no context\n"); return 1; }
    CGLSetCurrentContext(ctx);
    printf("GL %s / %s\n", glGetString(GL_VERSION), glGetString(GL_RENDERER));
    printf("ARB_buffer_storage: %s, ARB_map_buffer_range: %s, APPLE_flush_buffer_range: %s\n",
           strstr((const char *)glGetString(GL_EXTENSIONS), "GL_ARB_buffer_storage") ? "yes" : "no",
           strstr((const char *)glGetString(GL_EXTENSIONS), "GL_ARB_map_buffer_range") ? "yes" : "no",
           strstr((const char *)glGetString(GL_EXTENSIONS), "GL_APPLE_flush_buffer_range") ? "yes" : "no");

    GLuint fbo, rb;
    glGenFramebuffersEXT(1, &fbo); glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo);
    glGenRenderbuffersEXT(1, &rb); glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, rb);
    glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT, GL_RGBA8, W, H);
    glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_RENDERBUFFER_EXT, rb);
    glViewport(0, 0, W, H);

    prog = glCreateProgram();
    glAttachShader(prog, mkshader(GL_VERTEX_SHADER, "#version 120\nvarying vec2 t; void main(){ t = gl_MultiTexCoord0.xy; gl_Position = gl_Vertex; }"));
    glAttachShader(prog, mkshader(GL_FRAGMENT_SHADER, "#version 120\nuniform sampler2D s; uniform vec4 c; varying vec2 t; void main(){ gl_FragColor = texture2D(s, t) * c; }"));
    glLinkProgram(prog); glUseProgram(prog);
    uColor = glGetUniformLocation(prog, "c");

    static unsigned char pix[64 * 64 * 4];
    for (int i = 0; i < (int)sizeof(pix); i++) pix[i] = (unsigned char)(i * 13);
    glGenTextures(NTEX, tex);
    for (int i = 0; i < NTEX; i++)
    {
        glBindTexture(GL_TEXTURE_2D, tex[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, pix);
    }
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glGenBuffers(1, &vbo);

    const char *names[] = { "polymost today: VBO + glBufferSubData per polygon",
                            "client-side vertex arrays",
                            "glBufferData per polygon",
                            "reference: one upload per frame" };
    for (int mode = 0; mode < 4; mode++)
    {
        if (mode == 1)
        {
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            glVertexPointer(3, GL_FLOAT, 20, verts);
            glTexCoordPointer(2, GL_FLOAT, 20, verts + 3);
        }
        else
        {
            glBindBuffer(GL_ARRAY_BUFFER, vbo);
            glBufferData(GL_ARRAY_BUFFER, BUFVERTS * 20, NULL, GL_STREAM_DRAW);
            glVertexPointer(3, GL_FLOAT, 20, 0);
            glTexCoordPointer(2, GL_FLOAT, 20, (void *)12);
        }
        for (int i = 0; i < 20; i++) { frame(mode, npolys); glFlush(); }
        glFinish();
        double submit = 0, worst = 0, t0 = now_ms();
        for (int f = 0; f < FRAMES; f++)
        {
            double a = now_ms();
            frame(mode, npolys);
            glFlush();
            double d = now_ms() - a;
            submit += d;
            if (d > worst) worst = d;
        }
        glFinish();
        double total = now_ms() - t0;
        printf("%-52s %4d polys: submit %.2f ms/frame (worst %.2f), total %.2f ms/frame = %.0f fps, glGetError 0x%x\n",
               names[mode], npolys, submit / FRAMES, worst, total / FRAMES, 1000.0 * FRAMES / total, glGetError());
    }
    return 0;
}
