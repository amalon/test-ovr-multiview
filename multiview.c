/*
 * SPDX-FileCopyrightText: 2024 James Hogan <james@albanarts.com>
 * SPDX-License-Identifier: GPL-2.0-only
 */
#define GL_GLEXT_PROTOTYPES

#include <SDL2/SDL.h>
#include <GL/gl.h>
#include <stdbool.h>
#include <stdio.h>

#define TEX_WIDTH     400
#define TEX_HEIGHT    400
#define SCREEN_WIDTH  (TEX_WIDTH*2)
#define SCREEN_HEIGHT TEX_HEIGHT

static SDL_Window *window;
static unsigned int tex;
static unsigned int fbo[3];
static unsigned int num_fbos;
static unsigned int clear_prog[2];
static unsigned int scene_prog[2], buf_prog;
static unsigned int scene_list = 0;
static bool supports_multiview = false;
static bool multiview = false;
static bool display_lists = true;

/* Set up an array texture for multiview rendering */
static void setup_textures()
{
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D_ARRAY, tex);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGB8, TEX_WIDTH, TEX_HEIGHT, 2, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
}

/* Set up framebuffer object(s), for each layer of texture, or for all layers */
static int setup_fbo()
{
    unsigned int i;

    num_fbos = supports_multiview ? 3 : 2;
    glGenFramebuffers(num_fbos, fbo);

    for (i = 0; i < num_fbos; ++i) {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo[i]);

        if (i == 2) {
            glFramebufferTextureMultiviewOVR(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, tex, 0, 0, 2);
        } else {
            glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, tex, 0, i);
        }

        GLenum ret = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (ret != GL_FRAMEBUFFER_COMPLETE) {
            fprintf(stderr, "Framebuffer incomplete: %#x\n", ret);
            return 1;
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    return 0;
}

/* Build a shader program */
static unsigned int build_shader(const char *name,
                                 const char *vert_src, const char *frag_src)
{
    int success;
    unsigned int shader_vert, shader_frag, prog;
    char info_log[512];

    /* Vertex shader */
    shader_vert = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(shader_vert, 1, &vert_src, NULL);
    glCompileShader(shader_vert);
    glGetShaderiv(shader_vert, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(shader_vert, sizeof(info_log), NULL, info_log);
        fprintf(stderr, "%s vertex shader compilation failed: %s\n",
                name, info_log);
        return 0;
    }

    /* Fragment shader */
    shader_frag = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(shader_frag, 1, &frag_src, NULL);
    glCompileShader(shader_frag);
    glGetShaderiv(shader_frag, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(shader_frag, sizeof(info_log), NULL, info_log);
        fprintf(stderr, "%s fragment shader compilation failed: %s\n",
                name, info_log);
        return 0;
    }

    /* Shader program */
    prog = glCreateProgram();
    glAttachShader(prog, shader_vert);
    glAttachShader(prog, shader_frag);
    glLinkProgram(prog);
    glGetProgramiv(prog, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(prog, sizeof(info_log), NULL, info_log);
        fprintf(stderr, "%s shader link failed: %s\n", name, info_log);
        return 0;
    }

    glDeleteShader(shader_vert);
    glDeleteShader(shader_frag);

    return prog;
}

/* Set up the scene clear (multiview) shader */
static int setup_clear_shader(unsigned int multiview_version)
{
    const char *vert_src, *frag_src;
    if (multiview_version) {
        vert_src = "#version 330 core\n"
                   "#extension GL_OVR_multiview: enable\n"
                   "layout (num_views = 2) in;\n"
                   "layout (location = 0) in vec3 inPos;\n"
                   "void main()\n"
                   "{\n"
                   "  gl_Position = vec4(inPos, 1.0);\n"
                   "}\n";
    } else {
        vert_src = "#version 330 core\n"
                   "layout (location = 0) in vec3 inPos;\n"
                   "void main()\n"
                   "{\n"
                   "  gl_Position = vec4(inPos, 1.0);\n"
                   "}\n";
    }
    frag_src = "#version 330 core\n"
               "layout(location = 0) out vec4 fragColor;\n"
               "void main()\n"
               "{\n"
               "  fragColor = vec4(0.0, 0.0, 0.0, 0.0);\n"
               "}\n";

    clear_prog[multiview_version] = build_shader("Clear", vert_src, frag_src);
    return clear_prog[multiview_version] == 0;
}

/* Set up the scene rendering (multiview) shader */
static int setup_scene_shader(unsigned int multiview_version)
{
    const char *vert_src, *frag_src;
    if (multiview_version) {
        vert_src = "#version 330 core\n"
                   "#extension GL_OVR_multiview: enable\n"
                   "#extension GL_ARB_shader_viewport_layer_array: enable\n"
                   "layout (num_views = 2) in;\n"
                   "layout (location = 0) in vec3 inPos;\n"
                   "layout (location = 1) in vec3 inCol;\n"
                   "out vec3 color;\n"
                   "void main()\n"
                   "{\n"
                   "  gl_Position = vec4(inPos, 1.0);\n"
                   "  gl_ViewportIndex = int(gl_ViewID_OVR);\n"
                   "  color = inCol;\n"
                   "}\n";
    } else {
        vert_src = "#version 330 core\n"
                   "#extension GL_ARB_shader_viewport_layer_array: enable\n"
                   "layout (location = 0) in vec3 inPos;\n"
                   "layout (location = 1) in vec3 inCol;\n"
                   "out vec3 color;\n"
                   "void main()\n"
                   "{\n"
                   "  gl_Position = vec4(inPos, 1.0);\n"
                   "  gl_ViewportIndex = 1;\n"
                   "  color = inCol;\n"
                   "}\n";
    }
    frag_src = "#version 330 core\n"
               "layout(location = 0) out vec4 fragColor;\n"
               "in vec3 color;\n"
               "void main()\n"
               "{\n"
               "  fragColor = vec4(color, 1.0);\n"
               "}\n";

    scene_prog[multiview_version] = build_shader("Scene", vert_src, frag_src);
    return scene_prog[multiview_version] == 0;
}

/* Set up the shader for rendering the multiview texture to the window */
static int setup_buf_shader()
{
    const char *vert_src, *frag_src;
    vert_src = "#version 330 core\n"
               "layout (location = 0) in vec3 inPos;\n"
               "layout (location = 1) in vec3 inTexcoord;\n"
               "out vec3 texcoord;\n"
               "void main()\n"
               "{\n"
               "  gl_Position = vec4(inPos, 1.0);\n"
               "  texcoord = inTexcoord;\n"
               "}\n";
    frag_src = "#version 330 core\n"
               "uniform sampler2DArray tex;\n"
               "in vec3 texcoord;\n"
               "layout(location = 0) out vec4 fragColor;\n"
               "void main()\n"
               "{\n"
               "  fragColor = vec4(texture(tex, texcoord).rgb, 1.0);\n"
               "}\n";

    buf_prog = build_shader("Buffer", vert_src, frag_src);
    return buf_prog == 0;
}

static int setup()
{
    setup_textures();

    if (setup_fbo())
        return 1;

    if (supports_multiview && setup_clear_shader(1))
        return 1;

    if (supports_multiview && setup_scene_shader(1))
        return 1;

    if (setup_clear_shader(0))
        return 1;

    if (setup_scene_shader(0))
        return 1;

    if (setup_buf_shader())
        return 1;

    return 0;
}

static void cleanup()
{
    glDeleteLists(scene_list, 1);
    glDeleteFramebuffers(num_fbos, fbo);
    glDeleteTextures(1, &tex);
    glDeleteProgram(clear_prog[0]);
    glDeleteProgram(scene_prog[0]);
    if (supports_multiview) {
        glDeleteProgram(clear_prog[1]);
        glDeleteProgram(scene_prog[1]);
    }
    glDeleteProgram(buf_prog);
}

/* Render a full screen quad */
static void render_quad()
{
    float verts[3][3] = {
        /* x      y     z */
        {-1.0f, -1.0f, 0.0f},
        { 3.0f, -1.0f, 0.0f},
        {-1.0f,  3.0f, 0.0f},
    };
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, &verts[0][0]);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDisableVertexAttribArray(0);
}

/* Render a triangle */
static void render_triangle()
{
    float verts[3][3] = {
        /* x      y     z */
        {-0.5f, -0.5f, 0.0f},
        { 0.5f, -0.5f, 0.0f},
        {-0.5f,  0.5f, 0.0f},
    };
    float col[3][3] = {
        /* g      g     b */
        { 1.0f,  0.0f, 0.0f},
        { 0.0f,  1.0f, 0.0f},
        { 0.0f,  0.0f, 1.0f},
    };
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, &verts[0][0]);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, &col[0][0]);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
}

/* Clear the render buffers */
static void clear()
{
    glViewport(0, 0, TEX_WIDTH, TEX_HEIGHT);

    render_quad();
}

/* Render a single view of the scene */
static void render_scene()
{
    glViewport(0, 0, TEX_WIDTH, TEX_HEIGHT);
    glViewportIndexedf(0, 0, 0, TEX_WIDTH, TEX_HEIGHT);
    glViewportIndexedf(1, 0, 0, TEX_WIDTH, TEX_HEIGHT);

    if (display_lists) {
        if (!scene_list) {
            scene_list = glGenLists(1);
            glNewList(scene_list, GL_COMPILE);
            render_triangle();
            glEndList();
        }
        glCallList(scene_list);
    } else {
        render_triangle();
    }
}

/* Render a single layer of the multiview texture to the window */
static void render_buf_layer(unsigned int layer)
{
    float verts[4][3] = {
        /* x      y     z */
        {-1.0f, -1.0f, 0.0f},
        { 1.0f, -1.0f, 0.0f},
        {-1.0f,  1.0f, 0.0f},
        { 1.0f,  1.0f, 0.0f},
    };
    float coords[4][3] = {
        /* s    r    t */
        {0.0f, 0.0f, layer},
        {1.0f, 0.0f, layer},
        {0.0f, 1.0f, layer},
        {1.0f, 1.0f, layer},
    };
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, verts);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, coords);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
}

/* Render all layers of the multiview texture to the window */
static void render_buf()
{
    unsigned int i;

    glUseProgram(buf_prog);
    glBindTexture(GL_TEXTURE_2D_ARRAY, tex);

    for (i = 0; i < 2; ++i) {
        glViewport(i*TEX_WIDTH, 0, TEX_WIDTH, TEX_HEIGHT);
        render_buf_layer(i);
    }

    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
}

/* Perform all required rendering */
static void render()
{
    unsigned int i;

    /* Render the scene to FBO for each view (or just once with multiview) */
    for (i = multiview ? 2 : 0; i < (multiview ? 3 : 2); ++i) {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo[i]);
        glUseProgram(clear_prog[multiview ? 1 : 0]);
        clear();
        glUseProgram(scene_prog[multiview ? 1 : 0]);
        render_scene();
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    /* Render the FBO to the screen */
    render_buf();
    SDL_GL_SwapWindow(window);
}

static void checkExtensions()
{
    int count;
    unsigned int i;
    const char *name;

    /* Look for the GL_OVR_multiview extension */
    glGetIntegerv(GL_NUM_EXTENSIONS, &count);
    for(i = 0; i < count; ++i) {
        name = (const char *)glGetStringi(GL_EXTENSIONS, i);
        if (!strcmp(name, "GL_OVR_multiview")) {
            supports_multiview = true;
            break;
        }
    }

    multiview = supports_multiview;
    if (supports_multiview) {
        printf("GL_OVR_multiview is supported and ENABLED\n");
    } else {
        printf("GL_OVR_multiview is unsupported and DISABLED\n");
    }
}

static void updateWindowTitle()
{
    char buf[128];
    snprintf(buf, sizeof(buf),
             "Multiview test | Multiview: %s (%s) | Display lists: %s (D to toggle)",
             multiview ? "ON" : "OFF",
             supports_multiview ? "supported, M to toggle" : "unsupported",
             display_lists ? "ON" : "OFF");

    SDL_SetWindowTitle(window, buf);
}

int main(int argc, char **argv)
{
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
    window = SDL_CreateWindow("Multiview test",
                              SDL_WINDOWPOS_UNDEFINED,
                              SDL_WINDOWPOS_UNDEFINED,
                              SCREEN_WIDTH, SCREEN_HEIGHT,
                              SDL_WINDOW_OPENGL);
    SDL_GL_CreateContext(window);

    checkExtensions();

    if (setup())
        return EXIT_FAILURE;

    updateWindowTitle(window);
    for (;;) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                cleanup();
                return EXIT_SUCCESS;
            }
            if (event.type == SDL_KEYDOWN) {
                if (event.key.keysym.sym == SDLK_d) {
                    display_lists = !display_lists;
                    updateWindowTitle(window);
                    render();
                } else if (supports_multiview &&
                           event.key.keysym.sym == SDLK_m) {
                    multiview = !multiview;
                    updateWindowTitle(window);
                    render();
                }
            }

            if (event.type == SDL_WINDOWEVENT &&
                event.window.event == SDL_WINDOWEVENT_EXPOSED) {

                render();
            }
        }
    }
}
