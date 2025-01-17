/*
 * SPDX-FileCopyrightText: 2025 James Hogan <james@albanarts.com>
 * SPDX-License-Identifier: GPL-2.0-only
 */
#define GL_GLEXT_PROTOTYPES

#include <SDL2/SDL.h>
#include <GL/gl.h>
#include <stdbool.h>
#include <stdio.h>

#define VP_WIDTH 400
#define VP_HEIGHT 400
#define SCREEN_WIDTH  (2*VP_WIDTH)
#define SCREEN_HEIGHT VP_HEIGHT

static SDL_Window *window;
static unsigned int scene_prog[2];
static bool supports_viewport_array = false;
static bool viewport_array = false;

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

/* Set up the scene rendering shader */
static int setup_scene_shader(unsigned int viewport_array_version)
{
    const char *vert_src, *frag_src;
    if (viewport_array_version) {
        vert_src = "#version 330 core\n"
                   "#extension GL_ARB_shader_viewport_layer_array: enable\n"
                   "layout (location = 0) in vec3 inPos;\n"
                   "layout (location = 1) in vec3 inCol;\n"
                   "out vec3 color;\n"
                   "void main()\n"
                   "{\n"
                   "  gl_Position = vec4(inPos, 1.0);\n"
                   "  color = inCol;\n"
                   "  gl_ViewportIndex = 1;\n"
                   "}\n";
    } else {
        vert_src = "#version 330 core\n"
                   "layout (location = 0) in vec3 inPos;\n"
                   "layout (location = 1) in vec3 inCol;\n"
                   "out vec3 color;\n"
                   "void main()\n"
                   "{\n"
                   "  gl_Position = vec4(inPos, 1.0);\n"
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

    scene_prog[viewport_array_version] = build_shader("Scene", vert_src, frag_src);
    return scene_prog[viewport_array_version] == 0;
}

static int setup()
{
    if (supports_viewport_array && setup_scene_shader(1))
        return 1;

    if (setup_scene_shader(0))
        return 1;

    return 0;
}

static void cleanup()
{
    glDeleteProgram(scene_prog[0]);
    if (supports_viewport_array)
        glDeleteProgram(scene_prog[1]);
}

/* Render a triangle */
static void render_triangle(int vpi)
{
    GLfloat verts[3][3] = {
        /* x      y     z */
        {-0.5f, -0.5f, 0.0f},
        { 0.5f, -0.5f, 0.0f},
        {-0.5f,  0.5f, 0.0f},
    };
    GLfloat col[3][3] = {
        /* g      g     b */
        { vpi ? 0.0f : 1.0f, vpi ? 1.0f : 0.0f, 0.0f},
        { 0.0f, vpi ? 0.0f : 1.0f, vpi ? 1.0f : 0.0f},
        { vpi ? 1.0f : 0.0f, 0.0f, vpi ? 0.0f : 1.0f},
    };
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, &verts[0][0]);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, &col[0][0]);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
}

/* Perform all required rendering */
static void render()
{
    glClear(GL_COLOR_BUFFER_BIT);

    /* Render the scene without (left), then with (right) viewport array */

    glUseProgram(scene_prog[0]);
    glViewport(0, 0, VP_WIDTH, VP_HEIGHT);
    render_triangle(0);

    glUseProgram(scene_prog[viewport_array ? 1 : 0]);
    if (!viewport_array) {
        glViewport(VP_WIDTH, 0, VP_WIDTH, VP_HEIGHT);
    } else {
        glViewportIndexedf(0, 0, 0, VP_WIDTH, VP_HEIGHT);
        glViewportIndexedf(1, VP_WIDTH, 0, VP_WIDTH, VP_HEIGHT);
    }
    render_triangle(1);

    SDL_GL_SwapWindow(window);
}

static void checkExtensions()
{
    int count;
    unsigned int i;
    const char *name;

    /* Look for the GL_ARB_shader_viewport_layer_array extension */
    glGetIntegerv(GL_NUM_EXTENSIONS, &count);
    for(i = 0; i < count; ++i) {
        name = (const char *)glGetStringi(GL_EXTENSIONS, i);
        if (!strcmp(name, "GL_ARB_shader_viewport_layer_array")) {
            supports_viewport_array = true;
        }
    }

    viewport_array = supports_viewport_array;
    if (supports_viewport_array) {
        printf("GL_ARB_shader_viewport_layer_array is supported and ENABLED\n");
    } else {
        printf("GL_ARB_shader_viewport_layer_array is unsupported and DISABLED\n");
    }
}

static void updateWindowTitle()
{
    char buf[128];
    snprintf(buf, sizeof(buf),
             "Viewport Array test | Viewport Array: Left Off, Right %s (%s)",
             viewport_array ? "ON" : "OFF",
             supports_viewport_array ? "supported, V to toggle" : "unsupported");

    SDL_SetWindowTitle(window, buf);
}

int main(int argc, char **argv)
{
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
    window = SDL_CreateWindow("Viewport Array test",
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
                if (supports_viewport_array && event.key.keysym.sym == SDLK_v) {
                    viewport_array = !viewport_array;
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
