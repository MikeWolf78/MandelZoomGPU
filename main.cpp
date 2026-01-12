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

void main() {
    vec2 uv = (gl_FragCoord.xy - 0.5 * u_resolution.xy) / min(u_resolution.y, u_resolution.x);
    
    // We use double precision for the Mandelbrot calculation to allow deeper zooming
    dvec2 c = u_center + dvec2(uv) * u_zoom;
    dvec2 z = dvec2(0.0);
    int iter = 0;
    
    while (dot(z, z) < 4.0 && iter < u_maxIterations) {
        z = dvec2(z.x * z.x - z.y * z.y + c.x, 2.0 * z.x * z.y + c.y);
        iter++;
    }
    
    if (iter == u_maxIterations) {
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

bool dragging = false;
double lastMouseX = 0, lastMouseY = 0;

void scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
    double zoomFactor = (yoffset > 0) ? 0.9 : 1.1;
    
    // Scale mouse coordinates to framebuffer coordinates
    double fbMouseX = mouseX * (double)width / windowWidth;
    double fbMouseY = mouseY * (double)height / windowHeight;

    double minRes = std::min(width, height);
    double uv_x = (fbMouseX - 0.5 * width) / minRes;
    double uv_y = (0.5 * height - fbMouseY) / minRes;

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
        centerY += (fbDeltaY / minRes) * zoom;
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
    
    while (!glfwWindowShouldClose(window)) {
        glClear(GL_COLOR_BUFFER_BIT);
        
        glUseProgram(shaderProgram);
        glUniform2f(glGetUniformLocation(shaderProgram, "u_resolution"), (float)width, (float)height);
        glUniform2d(glGetUniformLocation(shaderProgram, "u_center"), centerX, centerY);
        glUniform1d(glGetUniformLocation(shaderProgram, "u_zoom"), zoom);
        glUniform1i(glGetUniformLocation(shaderProgram, "u_maxIterations"), maxIterations);
        
        glBindVertexArray(VAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        
        glfwSwapBuffers(window);
        glfwPollEvents();
        
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(window, true);
            
        // Optional: dynamically increase iterations as we zoom in
        maxIterations = 256 + (int)(-log10(zoom) * 100);
        if (maxIterations < 256) maxIterations = 256;
        if (maxIterations > 2000) maxIterations = 2000;
    }
    
    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    glDeleteProgram(shaderProgram);
    
    glfwTerminate();
    return 0;
}