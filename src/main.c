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

#define NROWS 40 // 400
#define NCOLS 40 // 400
#define POINT_COUNT (NROWS * NCOLS)
#define POINT_SIZE 0.05 //  0.001
#define POINT_ROUNDNESS 10
#define GRAV_CONSTANT 1e-10f
#define TIME_SPEED 1
static float total_energy;
static float velocities[POINT_COUNT][2];
static float coords[POINT_COUNT][3];
static float colors[POINT_COUNT][3];

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
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, NULL);
    glEnableVertexAttribArray(1);
    glBindBuffer(GL_ARRAY_BUFFER, colors_vbo_id);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, NULL);

    glProgramUniform1f(geometry_shader_id, 0, POINT_SIZE);
    glProgramUniform1i(geometry_shader_id, 1, POINT_ROUNDNESS);

    float top = 0.5f;
    float bottom = -0.5f;
    float left = -0.5f;
    float right = 0.5f;

    for (int y = 0; y < NROWS; ++y) {
        for (int x = 0; x < NCOLS; ++x) {
            int index = y * NCOLS + x;
            coords[index][0] = left + (right - left) * x / NCOLS;
            coords[index][1] = top + (bottom - top) * y / NROWS;
            velocities[index][0] = randf(-1e-4, 1e-4);
            velocities[index][1] = randf(-1e-4, 1e-4);
        }
    }
    for (int i = 0; i < POINT_COUNT; ++i) {
        for (int j = i + 1; j < POINT_COUNT; ++j) {
            float dx = coords[i][0] - coords[j][0];
            float dy = coords[i][1] - coords[j][1];
            float distance_squared = dx * dx + dy * dy;
            total_energy += GRAV_CONSTANT / distance_squared;
        }
    }
    printf("energy: %g\n", total_energy);
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

static void main_loop(void) {
    if (!prev_ticks) {
        prev_ticks = SDL_GetTicks();
    }
    long ticks = SDL_GetTicks();
    long delta_ticks = ticks - prev_ticks;
    float dt = TIME_SPEED * ((float) delta_ticks / 1000.);

//    size_t n = sizeof(colors) / sizeof(colors[0]);
//    for (size_t i = 0; i < n; ++i) {
//        colors[i][0] = 0.5f + sinf(alpha + i) / 2;
//        colors[i][1] = 0.5f + cosf(alpha + i) / 2;
//        colors[i][2] = 0.5f + cosf(alpha + 0.5f + i) / 2;
//    }
//    glBindBuffer(GL_ARRAY_BUFFER, colors_vbo_id);
//    glBufferData(GL_ARRAY_BUFFER, sizeof(colors), &colors, GL_STREAM_DRAW);

//    for (int y = 0; y < NROWS; ++y) {
//        for (int x = 0; x < NCOLS; ++x) {
//            int index = y * NCOLS + x;
//            coords[index][0] = coords[index][0] + (float) (rand() % 1000 - 500) / 100000;
//            coords[index][1] = coords[index][1] + (float) (rand() % 1000 - 500) / 100000;
//        }
//    }

    float current_total_potential_energy = 0;
    float current_total_kinetic_energy = 0;

    for (int i = 0; i < POINT_COUNT; ++i) {
        float acceleration[2] = { 0 };
        for (int j = 0; j < POINT_COUNT; ++j) {
            if (j == i) {
                continue;
            }
            float i_to_j[2] = {
                    coords[j][0] - coords[i][0],
                    coords[j][1] - coords[i][1]
            };
            float distance_squared = i_to_j[0] * i_to_j[0] + i_to_j[1] * i_to_j[1];
            if (distance_squared < 0.0000001f) {
                distance_squared = 0.0000001f;
            }
            acceleration[0] += i_to_j[0] / distance_squared;
            acceleration[1] += i_to_j[1] / distance_squared;

            current_total_potential_energy += GRAV_CONSTANT / distance_squared;
        }
        float abs_velocity_squared = velocities[i][0] * velocities[i][0] + velocities[i][1] * velocities[i][1];
        current_total_kinetic_energy += abs_velocity_squared / 2;

        float abs_velocity = 10000 * sqrtf(abs_velocity_squared);
        colors[i][0] = clamp(abs_velocity, 0, 1);
        colors[i][1] = clamp(abs_velocity, 0, 1);
        colors[i][2] = clamp(abs_velocity, 0, 1);
        acceleration[0] *= GRAV_CONSTANT;
        acceleration[1] *= GRAV_CONSTANT;
        coords[i][0] += velocities[i][0] * dt;
        coords[i][1] += velocities[i][1] * dt;
        velocities[i][0] += acceleration[0] * dt;
        velocities[i][1] += acceleration[1] * dt;
    }

    static int counter = 0;
    ++counter;
    if (0 == counter % 40) {
        printf("energy: %10g\tpotential: %10g\tkinetic: %10g\n",
               current_total_potential_energy + current_total_kinetic_energy,
               current_total_potential_energy, current_total_kinetic_energy);
    }

//    if (1e-8 < total_absolute_velocity) {
//        float velocity_error = -total_absolute_velocity;
//        for (int i = 0; i < POINT_COUNT; ++i) {
//            float fix_factor = 1 + velocity_error / total_absolute_velocity;
//            velocities[i][0] *= fix_factor;
//            velocities[i][1] *= fix_factor;
//        }
//    }

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
