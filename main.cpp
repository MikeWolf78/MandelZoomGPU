#define GL_SILENCE_DEPRECATION
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <OpenGL/gl3.h>
#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>

// Shaders
const char* vertexShaderSource = R"(
#version 410 core
layout (location = 0) in vec2 aPos;
void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

const char* fragmentShaderSource = R"(
#version 410 core
out vec4 FragColor;
uniform vec2 u_resolution;
uniform dvec2 u_center;
uniform double u_zoom;
uniform int u_maxIterations;
uniform sampler2D u_complexityMap;

void main() {
    vec2 uv = (gl_FragCoord.xy - 0.5 * u_resolution.xy) / min(u_resolution.y, u_resolution.x);
    
    // We use double precision for the Mandelbrot calculation to allow deeper zooming
    dvec2 c = u_center + dvec2(uv) * u_zoom;
    dvec2 z = dvec2(0.0);
    int iter = 0;
    
    // Get predicted max iterations from the complexity map
    float complexity = texture(u_complexityMap, gl_FragCoord.xy / u_resolution.xy).r;
    int predictedMaxIter = int(complexity * float(u_maxIterations));
    // Ensure at least some iterations to avoid black tiles
    if (predictedMaxIter < 16) predictedMaxIter = 16;

    while (dot(z, z) < 4.0 && iter < predictedMaxIter) {
        z = dvec2(z.x * z.x - z.y * z.y + c.x, 2.0 * z.x * z.y + c.y);
        iter++;
    }
    
    if (iter >= predictedMaxIter && predictedMaxIter < u_maxIterations) {
        // If we hit the predicted limit, we used to set it to u_maxIterations (black).
        // To avoid black artifacts when the prediction is slightly off, we'll
        // just use the iter value. It might cause slight color banding, but 
        // it's much better than black spots.
        // We still keep the 'black' for the real interior.
    }

    if (iter >= u_maxIterations) {
        FragColor = vec4(0.0, 0.0, 0.0, 1.0);
    } else {
        float t = float(iter) / float(u_maxIterations);
        // Simple coloring
        FragColor = vec4(0.5 + 0.5 * cos(3.0 + t * 10.0 + vec3(0.0, 0.6, 1.0)), 1.0);
    }
}
)";

// State
double centerX = -0.5, centerY = 0.0;
double zoom = 2.0;
int maxIterations = 256;
double mouseX = 0, mouseY = 0;
int width = 800, height = 600;
int windowWidth = 800, windowHeight = 600;

const int GRID_SIZE = 64;
float complexityMap[GRID_SIZE * GRID_SIZE];
GLuint complexityTexture;

bool dragging = false;
double lastMouseX = 0, lastMouseY = 0;

int getIterations(double c_re, double c_im, int maxIter) {
    double z_re = 0, z_im = 0;
    int iter = 0;
    while (z_re * z_re + z_im * z_im < 4.0 && iter < maxIter) {
        double next_re = z_re * z_re - z_im * z_im + c_re;
        z_im = 2.0 * z_re * z_im + c_im;
        z_re = next_re;
        iter++;
    }
    return iter;
}

void updateComplexityMap() {
    double minRes = std::min(width, height);
    
    for (int j = 0; j < GRID_SIZE; ++j) {
        for (int i = 0; i < GRID_SIZE; ++i) {
            // 3x3 sampling grid for each tile (9 samples)
            int maxTileIter = 0;
            int minTileIter = maxIterations;
            bool hitMax = false;
            
            for (int sy = 0; sy <= 2; ++sy) {
                for (int sx = 0; sx <= 2; ++sx) {
                    double x = (i + sx * 0.5) * width / GRID_SIZE;
                    double y = (j + sy * 0.5) * height / GRID_SIZE;
                    
                    double uv_x = (x - 0.5 * width) / minRes;
                    double uv_y = (y - 0.5 * height) / minRes;
                    
                    double c_re = centerX + uv_x * zoom;
                    double c_im = centerY + uv_y * zoom;
                    
                    int iter = getIterations(c_re, c_im, maxIterations);
                    if (iter >= maxIterations) hitMax = true;
                    if (iter > maxTileIter) maxTileIter = iter;
                    if (iter < minTileIter) minTileIter = iter;
                }
            }
            
            // Prediction:
            // 1. If any sample hits maxIterations, we must assume the boundary or interior is present.
            // 2. If there is a significant range in iterations, a boundary is nearby.
            // 3. We add a safety buffer to the predicted iterations.
            
            bool isSimple = !hitMax && (maxTileIter - minTileIter < 2);
            
            if (isSimple) {
                // Add a small buffer (e.g., 20% or at least 32) to handle non-sampled areas
                int buffer = std::max(32, (int)(maxTileIter * 0.2));
                int predicted = std::min(maxIterations, maxTileIter + buffer);
                complexityMap[j * GRID_SIZE + i] = (float)predicted / maxIterations;
            } else {
                complexityMap[j * GRID_SIZE + i] = 1.0f;
            }
        }
    }
    
    glBindTexture(GL_TEXTURE_2D, complexityTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, GRID_SIZE, GRID_SIZE, 0, GL_RED, GL_FLOAT, complexityMap);
}

void scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
    double zoomFactor = (yoffset > 0) ? 0.9 : 1.1;
    
    // Scale mouse coordinates to framebuffer coordinates
    double fbMouseX = mouseX * (double)width / windowWidth;
    double fbMouseY = mouseY * (double)height / windowHeight;

    double minRes = std::min(width, height);
    double uv_x = (fbMouseX - 0.5 * width) / minRes;
    double uv_y = (fbMouseY - 0.5 * height) / minRes;

    double oldZoom = zoom;
    zoom *= zoomFactor;
    
    centerX += uv_x * (oldZoom - zoom);
    centerY += uv_y * (oldZoom - zoom);
}

void cursor_position_callback(GLFWwindow* window, double xpos, double ypos) {
    if (dragging) {
        double deltaX = xpos - lastMouseX;
        double deltaY = ypos - lastMouseY;
        
        double fbDeltaX = deltaX * (double)width / windowWidth;
        double fbDeltaY = deltaY * (double)height / windowHeight;
        
        double minRes = std::min(width, height);
        centerX -= (fbDeltaX / minRes) * zoom;
        centerY -= (fbDeltaY / minRes) * zoom;
    }
    mouseX = xpos;
    mouseY = ypos;
    lastMouseX = xpos;
    lastMouseY = ypos;
}

void mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (action == GLFW_PRESS) {
            dragging = true;
            glfwGetCursorPos(window, &lastMouseX, &lastMouseY);
        } else if (action == GLFW_RELEASE) {
            dragging = false;
        }
    }
}

void framebuffer_size_callback(GLFWwindow* window, int w, int h) {
    width = w;
    height = h;
    glfwGetWindowSize(window, &windowWidth, &windowHeight);
    glViewport(0, 0, w, h);
}

GLuint compileShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    
    int success;
    char infoLog[512];
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(shader, 512, NULL, infoLog);
        std::cerr << "Shader Compilation Error: " << infoLog << std::endl;
    }
    return shader;
}

int main() {
    if (!glfwInit()) return -1;
    
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

    GLFWwindow* window = glfwCreateWindow(width, height, "Mandelbrot GPU", NULL, NULL);
    if (!window) {
        glfwTerminate();
        return -1;
    }
    
    glfwMakeContextCurrent(window);
    glfwSetScrollCallback(window, scroll_callback);
    glfwSetCursorPosCallback(window, cursor_position_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    
    // Get actual framebuffer and window size
    glfwGetFramebufferSize(window, &width, &height);
    glfwGetWindowSize(window, &windowWidth, &windowHeight);
    
    GLuint vertexShader = compileShader(GL_VERTEX_SHADER, vertexShaderSource);
    GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragmentShaderSource);
    
    GLuint shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);
    
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    
    float vertices[] = {
        -1.0f,  1.0f,
        -1.0f, -1.0f,
         1.0f, -1.0f,
        
        -1.0f,  1.0f,
         1.0f, -1.0f,
         1.0f,  1.0f
    };
    
    GLuint VBO, VAO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    
    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    
    // Initialize complexity texture
    glGenTextures(1, &complexityTexture);
    glBindTexture(GL_TEXTURE_2D, complexityTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    while (!glfwWindowShouldClose(window)) {
        // Optional: dynamically increase iterations as we zoom in
        maxIterations = 256 + (int)(-log10(zoom) * 100);
        if (maxIterations < 256) maxIterations = 256;
        if (maxIterations > 2000) maxIterations = 2000;

        updateComplexityMap();

        glClear(GL_COLOR_BUFFER_BIT);
        
        glUseProgram(shaderProgram);
        glUniform2f(glGetUniformLocation(shaderProgram, "u_resolution"), (float)width, (float)height);
        glUniform2d(glGetUniformLocation(shaderProgram, "u_center"), centerX, centerY);
        glUniform1d(glGetUniformLocation(shaderProgram, "u_zoom"), zoom);
        glUniform1i(glGetUniformLocation(shaderProgram, "u_maxIterations"), maxIterations);
        
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, complexityTexture);
        glUniform1i(glGetUniformLocation(shaderProgram, "u_complexityMap"), 0);

        glBindVertexArray(VAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        
        glfwSwapBuffers(window);
        glfwPollEvents();
        
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(window, true);
    }
    
    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    glDeleteTextures(1, &complexityTexture);
    glDeleteProgram(shaderProgram);
    
    glfwTerminate();
    return 0;
}