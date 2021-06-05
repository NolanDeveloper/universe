#include <stdio.h>
#include <stdbool.h>

#include <GL/glew.h>
#include <SDL.h>
#include <cglm/cglm.h>
#include <libpng/png.h>

#define KB(bytes) (1000 * (bytes))
#define MB(bytes) (1000 * KB(bytes))
#define GB(bytes) (1000 * MB(bytes))
#define TB(bytes) (1000 * GB(bytes))

#define SHADER_MAX_SIZE     MB(1)
#define IMAGE_MAX_SIZE      MB(20)

static SDL_Window * window = NULL;
static SDL_GLContext gl_context;

static GLuint coords_vbo_id;
static GLuint colors_vbo_id;
static GLuint vao_id;
static GLuint pipeline_id;

static GLint fragment_shader_id;
static GLint vertex_shader_id;
static GLint geometry_shader_id;

static size_t width, height;
static GLuint texture_id;

#define NROWS 30 // 400
#define NCOLS 30 // 400
#define POINT_COUNT 100000
#define POINT_SIZE 0.01f //  0.001
#define GRAV_CONSTANT 5e-11f
#define TIME_SPEED 1
#define INITIAL_SPEED 3e-2f
#define TOO_CLOSE 1e-6f
static vec2 velocities[POINT_COUNT];
static vec2 coords[POINT_COUNT];
static vec3 colors[POINT_COUNT];

static float masses[NROWS][NCOLS];
static vec2 forces[NROWS][NCOLS];

static bool read_file(const char *filename, char **content, bool is_binary, size_t max_size) {
    FILE *file = NULL;
    char *buffer = NULL;
    bool result = false;
    file = fopen(filename, (is_binary ? "rb" : "r"));
    if (!file) goto end;
    int err = fseek(file, 0, SEEK_END);
    if (err) goto end;
    long size = ftell(file);
    if (size < 0) goto end;
    if (max_size < (size_t) size) goto end;
    err = fseek(file, 0, SEEK_SET);
    if (err) goto end;
    buffer = malloc(size + 1);
    size_t filled = 0;
    while (filled < (size_t) size) {
        if (feof(file)) {
            goto end;
        }
        size_t n = fread(buffer + filled, 1, size - filled, file);
        if (ferror(file)) {
            goto end;
        }
        filled += n;
    }
    buffer[size] = '\0';
    *content = buffer;
    buffer = NULL;
    result = true;
end:
    if (file) {
        fclose(file);
    }
    free(buffer);
    return result;
}

static bool create_texture_from_file(const char * path, size_t *width, size_t *height, GLuint *texture_id) {
    bool result = false;
    png_image image;
    memset(&image, 0, (sizeof image));
    image.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_file(&image, path)) {
        goto end;
    }
    *width = image.width;
    *height = image.height;
    image.format = PNG_FORMAT_RGBA;
    png_bytep buffer = malloc(PNG_IMAGE_SIZE(image));
    if (!buffer) {
        goto end;
    }
    if (!png_image_finish_read(&image, NULL, buffer, 0, NULL)) {
        goto end;
    }
    glCreateTextures(GL_TEXTURE_2D, 1, texture_id);
    glBindTexture(GL_TEXTURE_2D, *texture_id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, *width, *height, 0, GL_RGBA, GL_UNSIGNED_BYTE, buffer);
    result = true;
end:
    return result;
}

void debug_callback(
        GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length,
        const GLchar* message, const void* userParam) {
    (void) type;
    (void) source;
    (void) id;
    (void) severity;
    (void) length;
    (void) userParam;
    printf("%s\n", message);
}

static float randf(float left, float right) {
    float r = rand() % 1000000 / 1000000.f;
    return left + r * (right - left);
}

static void initialize(void) {
    static GLchar shader_error_info[128];
    char *fragment_shader = NULL;
    char *vertex_shader   = NULL;
    char *geometry_shader = NULL;

    glEnable(GL_DEBUG_OUTPUT);
    glDebugMessageCallback(debug_callback, NULL);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    if (!read_file("../src/simple-fragment.fs", &fragment_shader, true, SHADER_MAX_SIZE)) {
        printf("Failed to read fragment shader from file");
        exit(EXIT_FAILURE);
    }
    fragment_shader_id = glCreateShaderProgramv(GL_FRAGMENT_SHADER, 1, (const GLchar *const *) &fragment_shader);
    if (!fragment_shader_id) {
        GLsizei length = sizeof(shader_error_info);
        glGetShaderInfoLog(GL_FRAGMENT_SHADER, 1, &length, shader_error_info);
        printf("glCreateProgram failed: %s", shader_error_info);
        exit(EXIT_FAILURE);
    }

    if (!read_file("../src/simple-vertex.vs", &vertex_shader, true, SHADER_MAX_SIZE)) {
        printf("Failed to read vertex shader from file");
        exit(EXIT_FAILURE);
    }
    vertex_shader_id = glCreateShaderProgramv(GL_VERTEX_SHADER, 1, (const GLchar *const *) &vertex_shader);
    if (!vertex_shader_id) {
        GLsizei length = sizeof(shader_error_info);
        glGetShaderInfoLog(GL_FRAGMENT_SHADER, 1, &length, shader_error_info);
        printf("glCreateProgram failed: %s", shader_error_info);
        exit(EXIT_FAILURE);
    }

    if (!read_file("../src/circle-geometry.gs", &geometry_shader, true, SHADER_MAX_SIZE)) {
        printf("Failed to read geometry shader from file");
        exit(EXIT_FAILURE);
    }
    geometry_shader_id = glCreateShaderProgramv(GL_GEOMETRY_SHADER, 1, (const GLchar *const *) &geometry_shader);
    if (!geometry_shader_id) {
        GLsizei length = sizeof(shader_error_info);
        glGetShaderInfoLog(GL_FRAGMENT_SHADER, 1, &length, shader_error_info);
        printf("glCreateProgram failed: %s", shader_error_info);
        exit(EXIT_FAILURE);
    }

    glGenProgramPipelines(1, &pipeline_id);
    glUseProgramStages(pipeline_id, GL_VERTEX_SHADER_BIT, vertex_shader_id);
    glUseProgramStages(pipeline_id, GL_FRAGMENT_SHADER_BIT, fragment_shader_id);
    glUseProgramStages(pipeline_id, GL_GEOMETRY_SHADER_BIT, geometry_shader_id);
    glBindProgramPipeline(pipeline_id);

    glGenBuffers(1, &coords_vbo_id);
    glGenBuffers(1, &colors_vbo_id);

    glGenVertexArrays(1, &vao_id);
    glBindVertexArray(vao_id);
    glEnableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, coords_vbo_id);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glEnableVertexAttribArray(1);
    glBindBuffer(GL_ARRAY_BUFFER, colors_vbo_id);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, NULL);

    glProgramUniform1f(geometry_shader_id, 0, POINT_SIZE);

    float s = 0.9f;
    float top = s;
    float bottom = -s;
    float left = -s;
    float right = s;

    for (int i = 0; i < POINT_COUNT; ++i) {
        float distance = randf(0.1f, s);
        float angle = randf(0, 2 * GLM_PIf);
        coords[i][0] = 1;
        coords[i][1] = 0;
        glm_vec2_rotate(coords[i], angle, coords[i]);
        glm_vec2_scale(coords[i], distance, coords[i]);
//        velocities[i][0] = randf(-INITIAL_SPEED, INITIAL_SPEED);
//        velocities[i][1] = randf(-INITIAL_SPEED, INITIAL_SPEED);
        glm_vec2_rotate(coords[i], GLM_PIf / 2, velocities[i]);
        glm_vec2_scale_as(velocities[i], INITIAL_SPEED, velocities[i]);
    }
    glBindBuffer(GL_ARRAY_BUFFER, coords_vbo_id);
    glBufferData(GL_ARRAY_BUFFER, sizeof(coords), &coords, GL_STREAM_DRAW);

    size_t n = sizeof(colors) / sizeof(colors[0]);
    for (size_t i = 0; i < n; ++i) {
        colors[i][0] = 1.f;
        colors[i][1] = 1.f;
        colors[i][2] = 1.f;
    }
    glBindBuffer(GL_ARRAY_BUFFER, colors_vbo_id);
    glBufferData(GL_ARRAY_BUFFER, sizeof(colors), &colors, GL_DYNAMIC_DRAW);

    if (!create_texture_from_file("../src/glow.png", &width, &height, &texture_id)) {
        goto end;
    }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture_id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_MIRRORED_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_MIRRORED_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glProgramUniform1i(fragment_shader_id, 0, 0);

end:
    free(fragment_shader);
    free(vertex_shader);
    free(geometry_shader);
}

static long prev_ticks;

static float clamp(float x, float bottom, float top) {
    if (x < bottom) {
        return bottom;
    }
    if (x < top) {
        return x;
    }
    return top;
}

static float rem(float a, float b) {
    return a - floorf(a / b) * b;
}

static float mod(float a, float b) {
    return a - truncf(a / b) * b;
}

static float repeat(float x, float bottom, float top) {
    float integral;
    float fraction = modf((x - bottom) / (top - bottom), &integral);
    if (fraction < 0) {
        fraction += 1;
    }
    return bottom + fraction * (top - bottom);
}

static void main_loop(void) {
    if (!prev_ticks) {
        prev_ticks = SDL_GetTicks();
    }
    long ticks = SDL_GetTicks();
    long delta_ticks = ticks - prev_ticks;
    prev_ticks = ticks;
    float dt = TIME_SPEED * ((float) delta_ticks / 1000.);

    float left = coords[0][0];
    float right = coords[0][0];
    float bottom = coords[0][1];
    float top = coords[0][1];
    for (int i = 0; i < POINT_COUNT; ++i) {
        coords[i][0] = repeat(coords[i][0], -1, 1);
        coords[i][1] = repeat(coords[i][1], -1, 1);
        if (coords[i][0] < left) {
            left = coords[i][0];
        }
        if (right < coords[i][0]) {
            right = coords[i][0];
        }
        if (coords[i][1] < bottom) {
            bottom = coords[i][1];
        }
        if (top < coords[i][1]) {
            top = coords[i][1];
        }
    }

    for (int i = 0; i < NROWS; ++i) {
        for (int j = 0; j < NCOLS; ++j) {
            masses[i][j] = 0;
        }
    }

    for (int i = 0; i < POINT_COUNT; ++i) {
        int row = (int) ((coords[i][1] - bottom) / (top - bottom) * (float) NROWS);
        int col = (int) ((coords[i][0] - left) / (right - left) * (float) NCOLS);
        if (row < 0) {
            row = 0;
        }
        if (NROWS - 1 < row) {
            row = NROWS - 1;
        }
        if (col < 0) {
            col = 0;
        }
        if (NCOLS - 1 < col) {
            col = NCOLS - 1;
        }
        ++masses[row][col];
    }

    for (int i1 = 0; i1 < NROWS; ++i1) {
        for (int j1 = 0; j1 < NCOLS; ++j1) {
            vec2 center1 = {
                    left + (right - left) / NCOLS * ((float) j1 + 0.5f),
                    bottom + (top - bottom) / NROWS * ((float) i1 + 0.5f),
            };
            glm_vec2_zero(forces[i1][j1]);
            for (int i2 = 0; i2 < NROWS; ++i2) {
                for (int j2 = 0; j2 < NCOLS; ++j2) {
                    if (i2 == i1 && j2 == j1) {
                        continue;
                    }
                    vec2 center2 = {
                            left + (right - left) / NCOLS * ((float) j2 + 0.5f),
                            bottom + (top - bottom) / NROWS * ((float) i2 + 0.5f),
                    };
                    float distance_squared = glm_vec2_distance2(center1, center2);
                    if (distance_squared < TOO_CLOSE) {
                        distance_squared = TOO_CLOSE;
                    }
                    float force_value = masses[i1][j1] * masses[i2][j2] / distance_squared;
                    vec2 force;
                    glm_vec2_sub(center2, center1, force);
                    glm_vec2_normalize(force);
                    glm_vec2_scale(force, force_value, force);
                    glm_vec2_add(forces[i1][j1], force, forces[i1][j1]);
                }
            }
            glm_vec2_scale(forces[i1][j1], GRAV_CONSTANT, forces[i1][j1]);
        }
    }

    for (int i = 0; i < POINT_COUNT; ++i) {
        int row = (int) ((coords[i][1] - bottom) / (top - bottom) * (float) NROWS);
        int col = (int) ((coords[i][0] - left) / (right - left) * (float) NCOLS);
        if (row < 0) {
            row = 0;
        }
        if (NROWS - 1 < row) {
            row = NROWS - 1;
        }
        if (col < 0) {
            col = 0;
        }
        if (NCOLS - 1 < col) {
            col = NCOLS - 1;
        }
        vec2 center = {
                left + (right - left) / NCOLS * ((float) col + 0.5f),
                bottom + (top - bottom) / NROWS * ((float) row + 0.5f),
        };
        vec2 to_center;
        glm_vec2_sub(center, coords[i], to_center);
        float distance_squared = glm_vec2_norm2(to_center);
        if (distance_squared < TOO_CLOSE) {
            distance_squared = TOO_CLOSE;
        }
        vec2 center_force;
        glm_vec2_scale_as(to_center, GRAV_CONSTANT * masses[row][col] / distance_squared, center_force);
        vec2 delta_velocity;
        glm_vec2(forces[row][col], delta_velocity);
        if (1 < masses[row][col]) {
            glm_vec2_add(delta_velocity, center_force, delta_velocity);
        }
        glm_vec2_scale(delta_velocity, dt, delta_velocity);
        glm_vec2_add(velocities[i], delta_velocity, velocities[i]);
        vec2 delta_coord;
        glm_vec2(velocities[i], delta_coord);
        glm_vec2_scale(delta_coord, dt, delta_coord);
        glm_vec2_add(coords[i], delta_coord, coords[i]);
    }

    glBindBuffer(GL_ARRAY_BUFFER, colors_vbo_id);
    glBufferData(GL_ARRAY_BUFFER, sizeof(colors), &colors, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, coords_vbo_id);
    glBufferData(GL_ARRAY_BUFFER, sizeof(coords), &coords, GL_STREAM_DRAW);

    glClearColor(0.1, 0.1, 0.1, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawArrays(GL_POINTS, 0, POINT_COUNT);
}

int main() {
    int err = SDL_Init(SDL_INIT_TIMER | SDL_INIT_VIDEO | SDL_INIT_EVENTS);
    if (err) {
        SDL_Log("Unable to initialize SDL: %s", SDL_GetError());
        exit(EXIT_FAILURE);
    }

    err = SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
    if (err) {
        SDL_Log("SDL_GL_SetAttribute failed: %s", SDL_GetError());
        exit(EXIT_FAILURE);
    }
    err = SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);
    if (err) {
        SDL_Log("SDL_GL_SetAttribute failed: %s", SDL_GetError());
        exit(EXIT_FAILURE);
    }
    err = SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    if (err) {
        SDL_Log("SDL_GL_SetAttribute failed: %s", SDL_GetError());
        exit(EXIT_FAILURE);
    }
    err = SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    if (err) {
        SDL_Log("SDL_GL_SetAttribute failed: %s", SDL_GetError());
        exit(EXIT_FAILURE);
    }

    window = SDL_CreateWindow("Universe", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 800, 800, SDL_WINDOW_OPENGL);
    if (!window) {
        SDL_Log("Unable to create window: %s", SDL_GetError());
        exit(EXIT_FAILURE);
    }

    gl_context = SDL_GL_CreateContext(window);

    err = glewInit();
    if (err) {
        SDL_Log("Unable to initialize glew: %s", glewGetErrorString(err));
        exit(EXIT_FAILURE);
    }

    initialize();

    SDL_Event event;
    bool is_running = true;
    bool is_mouse_down = false;
    while (is_running) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT || (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_q)) {
                is_running = false;
            } else if (event.type == SDL_MOUSEBUTTONDOWN || (is_mouse_down && event.type == SDL_MOUSEMOTION)) {
                if (event.type == SDL_MOUSEBUTTONDOWN) {
                    is_mouse_down = true;
                }
                int w, h;
                SDL_GetWindowSize(window, &w, &h);
                coords[0][0] = 2.f * ((float) event.button.x / w - 0.5f);
                coords[0][1] = 2.f * (1.f - (float) event.button.y / h - 0.5f);
//                velocities[0][0] = 0.f;
//                velocities[0][1] = 0.f;
            } else if (event.type == SDL_MOUSEBUTTONUP) {
                is_mouse_down = false;
            }
        }
        main_loop();
        SDL_GL_SwapWindow(window);
    }

    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
