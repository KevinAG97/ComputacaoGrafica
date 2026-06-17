/* DesafioM6 - Trajetórias cíclicas para objetos da cena
 *
 * Módulo 6 - Computação Gráfica - Unisinos
 * Autor: Kevin de Azevedo Garcia
 *
 * Extensão do DesafioM5 (câmera FPS + iluminação Phong).
 * Cada objeto da cena possui uma lista de pontos de controle (waypoints)
 * que define uma trajetória cíclica. O objeto se translada linearmente
 * de waypoint em waypoint, voltando ao primeiro ao atingir o último.
 *
 * Mecanismo de adição de pontos (via teclado):
 *   Tab              : seleciona o próximo objeto
 *   P                : adiciona waypoint na posição atual da câmera
 *   C                : limpa os waypoints do objeto selecionado
 *   Enter            : liga / desliga animação de todos os objetos
 *   +  /  -          : aumenta / diminui velocidade da trajetória
 *   X                : salva waypoints de todos os objetos em "waypoints.txt"
 *
 * Câmera FPS:
 *   W / A / S / D    : mover câmera
 *   Espaço / LShift  : subir / descer
 *   Mouse            : olhar ao redor
 *   M                : capturar / liberar cursor
 *   1 / 2 / 3        : toggle Key / Fill / Back light
 *   Scroll           : zoom (FOV)
 *   ESC              : sair
 */

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cmath>

using namespace std;

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

using namespace glm;

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

// =========================================================
//  Vertex Shader
// =========================================================
const GLchar* vertexShaderSource = R"(
#version 450
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 color;
layout(location = 2) in vec3 normal;
layout(location = 3) in vec2 texCoord;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

out vec3 vNormal;
out vec3 fragPos;
out vec3 vColor;
out vec2 fragTexCoord;

void main()
{
    vec4 worldPos  = model * vec4(position, 1.0);
    gl_Position    = projection * view * worldPos;
    fragPos        = vec3(worldPos);
    vNormal        = mat3(transpose(inverse(model))) * normal;
    vColor         = color;
    fragTexCoord   = texCoord;
}
)";

// =========================================================
//  Fragment Shader — Phong com 3 luzes pontuais + textura
// =========================================================
const GLchar* fragmentShaderSource = R"(
#version 450

in vec3 vNormal;
in vec3 fragPos;
in vec3 vColor;
in vec2 fragTexCoord;

out vec4 color;

struct PointLight {
    vec3  position;
    vec3  color;
    float intensity;
    int   enabled;
    float constant;
    float linear;
    float quadratic;
};

uniform PointLight lights[3];
uniform sampler2D  texBuff;
uniform int        useTexture;
uniform vec3       camPos;
uniform float      ka;
uniform float      kd;
uniform float      ks;
uniform float      q;

void main()
{
    vec3 objColor = (useTexture == 1)
                    ? vec3(texture(texBuff, fragTexCoord))
                    : vColor;

    vec3 N = normalize(vNormal);
    vec3 V = normalize(camPos - fragPos);

    vec3 result = ka * objColor;

    for (int i = 0; i < 3; i++)
    {
        if (lights[i].enabled == 0) continue;

        vec3  L    = normalize(lights[i].position - fragPos);
        float dist = length(lights[i].position - fragPos);

        float att = 1.0 / (lights[i].constant
                         + lights[i].linear    * dist
                         + lights[i].quadratic * dist * dist);

        float diff    = max(dot(N, L), 0.0);
        vec3  diffuse = kd * diff * lights[i].color * lights[i].intensity * att;

        vec3  R    = normalize(reflect(-L, N));
        float spec = pow(max(dot(R, V), 0.0), q);
        vec3  specular = ks * spec * lights[i].color * lights[i].intensity;

        result += diffuse * objColor + specular;
    }

    color = vec4(result, 1.0);
}
)";

// =========================================================
//  Classe Camera (FPS)
// =========================================================
enum CameraDirection { FORWARD, BACKWARD, LEFT, RIGHT, UP, DOWN };

class Camera
{
public:
    vec3  position;
    vec3  front;
    vec3  up;
    vec3  right;
    vec3  worldUp;
    float yaw;
    float pitch;
    float speed;
    float sensitivity;
    float fov;

    Camera(vec3  pos    = vec3(0.0f, 0.5f, 5.0f),
           vec3  wUp    = vec3(0.0f, 1.0f, 0.0f),
           float yaw    = -90.0f,
           float pitch  = 0.0f)
        : position(pos), worldUp(wUp), yaw(yaw), pitch(pitch),
          speed(3.5f), sensitivity(0.10f), fov(45.0f)
    { atualizarVetores(); }

    mat4 getViewMatrix() const { return lookAt(position, position + front, up); }

    void mover(CameraDirection dir, float dt)
    {
        float vel = speed * dt;
        switch (dir) {
            case FORWARD:  position += front   * vel; break;
            case BACKWARD: position -= front   * vel; break;
            case LEFT:     position -= right   * vel; break;
            case RIGHT:    position += right   * vel; break;
            case UP:       position += worldUp * vel; break;
            case DOWN:     position -= worldUp * vel; break;
        }
    }

    void rotacionar(float xoff, float yoff)
    {
        yaw   += xoff * sensitivity;
        pitch += yoff * sensitivity;
        if (pitch >  89.0f) pitch =  89.0f;
        if (pitch < -89.0f) pitch = -89.0f;
        atualizarVetores();
    }

    void zoom(float yoff)
    {
        fov -= yoff;
        if (fov <  5.0f) fov =  5.0f;
        if (fov > 90.0f) fov = 90.0f;
    }

private:
    void atualizarVetores()
    {
        vec3 f;
        f.x   = cos(radians(yaw)) * cos(radians(pitch));
        f.y   = sin(radians(pitch));
        f.z   = sin(radians(yaw)) * cos(radians(pitch));
        front = normalize(f);
        right = normalize(cross(front, worldUp));
        up    = normalize(cross(right, front));
    }
};

// =========================================================
//  Struct de trajetória — lista de waypoints + estado de animação
// =========================================================
struct Trajectory
{
    vector<vec3> waypoints;
    int   currentIdx = 0;   // waypoint de origem atual
    float t          = 0.0f; // blend [0,1] entre currentIdx e próximo
    float speed      = 1.5f; // unidades por segundo

    // Avança a trajetória por deltaTime.
    // Retorna a posição interpolada linearmente, ou posBase se vazio.
    vec3 update(float dt, vec3 posBase)
    {
        if (waypoints.size() < 2) return posBase;

        int next = (currentIdx + 1) % (int)waypoints.size();
        float segLen = length(waypoints[next] - waypoints[currentIdx]);

        // Evita divisão por zero em waypoints sobrepostos
        float dt_norm = (segLen > 0.0001f) ? (speed * dt / segLen) : 1.0f;
        t += dt_norm;

        while (t >= 1.0f) {
            t -= 1.0f;
            currentIdx = next;
            next = (currentIdx + 1) % (int)waypoints.size();
            segLen = length(waypoints[next] - waypoints[currentIdx]);
            dt_norm = (segLen > 0.0001f) ? (speed * t / segLen) : 1.0f;
        }

        return mix(waypoints[currentIdx],
                   waypoints[(currentIdx + 1) % (int)waypoints.size()], t);
    }
};

// =========================================================
//  Structs auxiliares
// =========================================================
struct OBJModel
{
    GLuint     VAO        = 0;
    int        nVertices  = 0;
    GLuint     texID      = 0;
    vec3       basePos;         // posição inicial (sem trajetória)
    vec3       position;        // posição atual (animada ou basePos)
    float      scale      = 1.0f;
    vec3       color      = vec3(0.8f, 0.7f, 0.6f);
    string     name;
    Trajectory traj;
};

struct PointLight
{
    vec3   position;
    vec3   color;
    float  intensity;
    bool   enabled;
    float  constant, linear, quadratic;
    string name;
};

// =========================================================
//  Globals
// =========================================================
const GLuint WIDTH = 1200, HEIGHT = 800;

Camera     gCamera;
PointLight gLights[3];

float lastX      = WIDTH  / 2.0f;
float lastY      = HEIGHT / 2.0f;
bool  firstMouse     = true;
bool  mouseCaptured  = true;

// Estado da seleção e animação
int  gSelectedObj  = 0;
bool gAnimate      = true;

// Referência para a lista de objetos (preenchida em main)
vector<OBJModel>* gObjects = nullptr;

// =========================================================
//  Protótipos
// =========================================================
void   key_callback(GLFWwindow*, int, int, int, int);
void   mouse_callback(GLFWwindow*, double, double);
void   scroll_callback(GLFWwindow*, double, double);
int    setupShader();
GLuint loadTexture(const string&);
string loadMTL(const string&);
GLuint loadOBJWithNormals(const string&, int&, GLuint&, vec3);
GLuint generateSphereSimple(float, int, int, int&, vec3);
void   sendLightUniforms(GLuint);
void   salvarWaypoints(const vector<OBJModel>&);

// =========================================================
//  main
// =========================================================
int main()
{
    glfwInit();
    GLFWwindow* window = glfwCreateWindow(WIDTH, HEIGHT,
        "DesafioM6 - Trajetorias Ciclicas -- Kevin Garcia", nullptr, nullptr);
    glfwMakeContextCurrent(window);

    glfwSetKeyCallback(window, key_callback);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetScrollCallback(window, scroll_callback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
        cout << "Failed to initialize GLAD" << endl;

    cout << "Renderer : " << glGetString(GL_RENDERER) << endl;
    cout << "OpenGL   : " << glGetString(GL_VERSION)  << endl;

    int w, h;
    glfwGetFramebufferSize(window, &w, &h);
    glViewport(0, 0, w, h);

    GLuint shaderID = setupShader();
    glUseProgram(shaderID);

    // --- Cena ---
    vector<OBJModel> objects;

    // Suzanne — ao centro
    {
        OBJModel m;
        m.name     = "Suzanne";
        m.color    = vec3(0.85f, 0.72f, 0.60f);
        m.basePos  = vec3(0.0f, 0.0f, 0.0f);
        m.position = m.basePos;
        m.scale    = 1.0f;
        m.VAO      = loadOBJWithNormals("../assets/Modelos3D/Suzanne.obj",
                                        m.nVertices, m.texID, m.color);
        // Trajetória padrão: triângulo em torno da origem
        m.traj.waypoints = {
            vec3( 0.0f, 0.0f,  0.0f),
            vec3( 3.0f, 0.0f,  3.0f),
            vec3(-3.0f, 0.0f,  3.0f),
            vec3( 0.0f, 2.0f, -2.0f)
        };
        if (m.VAO) objects.push_back(m);
    }
    // Cubo azul — à direita
    {
        OBJModel m;
        m.name     = "Cubo Azul";
        m.color    = vec3(0.3f, 0.6f, 1.0f);
        m.basePos  = vec3(3.5f, 0.0f, -2.0f);
        m.position = m.basePos;
        m.scale    = 1.2f;
        m.VAO      = loadOBJWithNormals("../assets/Modelos3D/Cube.obj",
                                        m.nVertices, m.texID, m.color);
        // Trajetória padrão: vai e volta numa linha
        m.traj.waypoints = {
            vec3( 3.5f, 0.0f, -2.0f),
            vec3( 3.5f, 3.0f, -2.0f),
            vec3( 6.0f, 3.0f, -2.0f),
            vec3( 6.0f, 0.0f, -2.0f)
        };
        if (m.VAO) objects.push_back(m);
    }
    // Cubo verde — à esquerda
    {
        OBJModel m;
        m.name     = "Cubo Verde";
        m.color    = vec3(0.4f, 1.0f, 0.5f);
        m.basePos  = vec3(-4.0f, 0.5f, -3.5f);
        m.position = m.basePos;
        m.scale    = 0.8f;
        m.VAO      = loadOBJWithNormals("../assets/Modelos3D/Cube.obj",
                                        m.nVertices, m.texID, m.color);
        // Trajetória padrão: círculo aproximado
        m.traj.waypoints = {
            vec3(-4.0f, 0.5f, -3.5f),
            vec3(-6.0f, 0.5f, -1.5f),
            vec3(-4.0f, 0.5f,  0.5f),
            vec3(-2.0f, 0.5f, -1.5f)
        };
        if (m.VAO) objects.push_back(m);
    }

    if (objects.empty()) {
        cerr << "Nenhum modelo carregado. Encerrando." << endl;
        glfwTerminate();
        return -1;
    }

    gObjects = &objects;

    // --- Iluminação de três pontos ---
    gLights[0] = { vec3(-4.0f, 5.0f,  4.0f), vec3(1.00f,0.95f,0.80f),
                   1.0f, true, 1.0f,0.09f,0.032f, "Key Light  (1)" };
    gLights[1] = { vec3( 5.0f, 3.0f,  4.0f), vec3(0.70f,0.80f,1.00f),
                   0.5f, true, 1.0f,0.14f,0.070f, "Fill Light (2)" };
    gLights[2] = { vec3( 0.5f, 6.0f, -5.0f), vec3(0.90f,0.90f,1.00f),
                   0.7f, true, 1.0f,0.10f,0.045f, "Back Light (3)" };

    // Esferas marcadoras de luz
    int    nSphereVerts;
    GLuint sphereVAO[3];
    for (int i = 0; i < 3; i++)
        sphereVAO[i] = generateSphereSimple(0.08f, 12, 12, nSphereVerts, gLights[i].color);

    // Esfera marcadora de waypoint (amarela)
    int    nWpVerts;
    GLuint wpVAO = generateSphereSimple(0.12f, 8, 8, nWpVerts, vec3(1.0f, 0.9f, 0.1f));

    // --- Locs de uniforms ---
    GLint modelLoc      = glGetUniformLocation(shaderID, "model");
    GLint viewLoc       = glGetUniformLocation(shaderID, "view");
    GLint projLoc       = glGetUniformLocation(shaderID, "projection");
    GLint camLoc        = glGetUniformLocation(shaderID, "camPos");
    GLint useTextureLoc = glGetUniformLocation(shaderID, "useTexture");
    GLint texBuffLoc    = glGetUniformLocation(shaderID, "texBuff");
    GLint kaLoc         = glGetUniformLocation(shaderID, "ka");
    GLint kdLoc         = glGetUniformLocation(shaderID, "kd");
    GLint ksLoc         = glGetUniformLocation(shaderID, "ks");
    GLint qLoc          = glGetUniformLocation(shaderID, "q");

    glUniform1i(texBuffLoc, 0);
    glActiveTexture(GL_TEXTURE0);
    glUniform1f(qLoc, 32.0f);
    glEnable(GL_DEPTH_TEST);

    cout << "\n=== DesafioM6 - Trajetorias ===" << endl;
    cout << "W/A/S/D        : mover camera" << endl;
    cout << "Espaco/LShift  : subir / descer" << endl;
    cout << "Mouse          : olhar ao redor" << endl;
    cout << "M              : capturar / liberar cursor" << endl;
    cout << "1 / 2 / 3      : toggle Key / Fill / Back light" << endl;
    cout << "Tab            : selecionar proximo objeto" << endl;
    cout << "P              : adicionar waypoint na posicao da camera" << endl;
    cout << "C              : limpar waypoints do objeto selecionado" << endl;
    cout << "Enter          : ligar / desligar animacao" << endl;
    cout << "+  / -         : aumentar / diminuir velocidade" << endl;
    cout << "X              : salvar waypoints em arquivo" << endl;
    cout << "ESC            : sair" << endl;
    cout << "\nObjeto selecionado: " << objects[gSelectedObj].name << endl;
    cout << "Animacao: " << (gAnimate ? "LIGADA" : "DESLIGADA") << endl;

    float lastFrame = 0.0f;

    // =========================================================
    //  Loop principal
    // =========================================================
    while (!glfwWindowShouldClose(window))
    {
        float currentFrame = (float)glfwGetTime();
        float deltaTime    = currentFrame - lastFrame;
        lastFrame          = currentFrame;

        glfwPollEvents();

        // Movimento contínuo da câmera
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
            gCamera.mover(FORWARD,  deltaTime);
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
            gCamera.mover(BACKWARD, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
            gCamera.mover(LEFT,     deltaTime);
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
            gCamera.mover(RIGHT,    deltaTime);
        if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)
            gCamera.mover(UP,       deltaTime);
        if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
            gCamera.mover(DOWN,     deltaTime);

        // Atualizar trajetórias
        if (gAnimate) {
            for (auto& obj : objects)
                obj.position = obj.traj.update(deltaTime, obj.basePos);
        }

        glClearColor(0.05f, 0.05f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        mat4 view = gCamera.getViewMatrix();
        mat4 proj = perspective(radians(gCamera.fov),
                                (float)WIDTH / HEIGHT, 0.1f, 100.0f);

        glUniformMatrix4fv(viewLoc, 1, GL_FALSE, value_ptr(view));
        glUniformMatrix4fv(projLoc, 1, GL_FALSE, value_ptr(proj));
        glUniform3fv(camLoc, 1, value_ptr(gCamera.position));

        sendLightUniforms(shaderID);

        // ---------------------------------------------------------
        //  Desenhar objetos da cena
        // ---------------------------------------------------------
        glUniform1f(kaLoc, 0.10f);
        glUniform1f(kdLoc, 0.70f);
        glUniform1f(ksLoc, 0.50f);

        for (int i = 0; i < (int)objects.size(); i++)
        {
            auto& obj = objects[i];
            mat4 model = translate(mat4(1.0f), obj.position);
            model      = scale(model, vec3(obj.scale));
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, value_ptr(model));

            if (obj.texID != 0) {
                glUniform1i(useTextureLoc, 1);
                glBindTexture(GL_TEXTURE_2D, obj.texID);
            } else {
                glUniform1i(useTextureLoc, 0);
                glBindTexture(GL_TEXTURE_2D, 0);
            }

            glBindVertexArray(obj.VAO);
            glDrawArrays(GL_TRIANGLES, 0, obj.nVertices);
            glBindVertexArray(0);
            glBindTexture(GL_TEXTURE_2D, 0);
        }

        // ---------------------------------------------------------
        //  Marcadores de luz (esferas emissivas)
        // ---------------------------------------------------------
        glUniform1i(useTextureLoc, 0);
        glUniform1f(kaLoc, 1.0f);
        glUniform1f(kdLoc, 0.0f);
        glUniform1f(ksLoc, 0.0f);

        for (int i = 0; i < 3; i++) {
            if (!gLights[i].enabled) continue;
            mat4 model = translate(mat4(1.0f), gLights[i].position);
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, value_ptr(model));
            glBindVertexArray(sphereVAO[i]);
            glDrawArrays(GL_TRIANGLES, 0, nSphereVerts);
            glBindVertexArray(0);
        }

        // ---------------------------------------------------------
        //  Marcadores de waypoints do objeto selecionado (amarelos)
        // ---------------------------------------------------------
        if (gSelectedObj < (int)objects.size()) {
            auto& sel = objects[gSelectedObj];
            for (auto& wp : sel.traj.waypoints) {
                mat4 model = translate(mat4(1.0f), wp);
                glUniformMatrix4fv(modelLoc, 1, GL_FALSE, value_ptr(model));
                glBindVertexArray(wpVAO);
                glDrawArrays(GL_TRIANGLES, 0, nWpVerts);
                glBindVertexArray(0);
            }
        }

        glfwSwapBuffers(window);
    }

    for (auto& obj : objects) {
        glDeleteVertexArrays(1, &obj.VAO);
        if (obj.texID) glDeleteTextures(1, &obj.texID);
    }
    for (int i = 0; i < 3; i++) glDeleteVertexArrays(1, &sphereVAO[i]);
    glDeleteVertexArrays(1, &wpVAO);
    glfwTerminate();
    return 0;
}

// =========================================================
//  key_callback
// =========================================================
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mode)
{
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, GL_TRUE);
        return;
    }

    // Cursor
    if (key == GLFW_KEY_M && action == GLFW_PRESS) {
        mouseCaptured = !mouseCaptured;
        firstMouse    = true;
        glfwSetInputMode(window, GLFW_CURSOR,
            mouseCaptured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
        cout << "Cursor: " << (mouseCaptured ? "capturado" : "livre") << endl;
    }

    // Toggle luzes
    if (key == GLFW_KEY_1 && action == GLFW_PRESS) {
        gLights[0].enabled = !gLights[0].enabled;
        cout << gLights[0].name << ": " << (gLights[0].enabled?"LIGADA":"DESLIGADA") << endl;
    }
    if (key == GLFW_KEY_2 && action == GLFW_PRESS) {
        gLights[1].enabled = !gLights[1].enabled;
        cout << gLights[1].name << ": " << (gLights[1].enabled?"LIGADA":"DESLIGADA") << endl;
    }
    if (key == GLFW_KEY_3 && action == GLFW_PRESS) {
        gLights[2].enabled = !gLights[2].enabled;
        cout << gLights[2].name << ": " << (gLights[2].enabled?"LIGADA":"DESLIGADA") << endl;
    }

    if (!gObjects || gObjects->empty()) return;
    auto& objects = *gObjects;

    // Selecionar próximo objeto
    if (key == GLFW_KEY_TAB && action == GLFW_PRESS) {
        gSelectedObj = (gSelectedObj + 1) % (int)objects.size();
        cout << "Objeto selecionado: " << objects[gSelectedObj].name
             << "  (waypoints: " << objects[gSelectedObj].traj.waypoints.size() << ")" << endl;
    }

    // Adicionar waypoint na posição da câmera
    if (key == GLFW_KEY_P && action == GLFW_PRESS) {
        vec3 wp = gCamera.position;
        objects[gSelectedObj].traj.waypoints.push_back(wp);
        cout << "Waypoint adicionado em ("
             << wp.x << ", " << wp.y << ", " << wp.z << ")  "
             << "Total: " << objects[gSelectedObj].traj.waypoints.size()
             << "  [" << objects[gSelectedObj].name << "]" << endl;
    }

    // Limpar waypoints do objeto selecionado
    if (key == GLFW_KEY_C && action == GLFW_PRESS) {
        objects[gSelectedObj].traj.waypoints.clear();
        objects[gSelectedObj].traj.currentIdx = 0;
        objects[gSelectedObj].traj.t          = 0.0f;
        objects[gSelectedObj].position        = objects[gSelectedObj].basePos;
        cout << "Waypoints limpos: " << objects[gSelectedObj].name << endl;
    }

    // Toggle animação
    if (key == GLFW_KEY_ENTER && action == GLFW_PRESS) {
        gAnimate = !gAnimate;
        cout << "Animacao: " << (gAnimate ? "LIGADA" : "DESLIGADA") << endl;
    }

    // Velocidade
    if (key == GLFW_KEY_EQUAL && action == GLFW_PRESS) {
        for (auto& obj : objects) obj.traj.speed += 0.5f;
        cout << "Velocidade: " << objects[gSelectedObj].traj.speed << " u/s" << endl;
    }
    if (key == GLFW_KEY_MINUS && action == GLFW_PRESS) {
        for (auto& obj : objects) {
            obj.traj.speed -= 0.5f;
            if (obj.traj.speed < 0.1f) obj.traj.speed = 0.1f;
        }
        cout << "Velocidade: " << objects[gSelectedObj].traj.speed << " u/s" << endl;
    }

    // Salvar waypoints
    if (key == GLFW_KEY_X && action == GLFW_PRESS) {
        salvarWaypoints(objects);
    }
}

// =========================================================
//  mouse_callback
// =========================================================
void mouse_callback(GLFWwindow* window, double xpos, double ypos)
{
    if (!mouseCaptured) return;
    if (firstMouse) { lastX=(float)xpos; lastY=(float)ypos; firstMouse=false; }

    float xoff =  (float)xpos - lastX;
    float yoff =  lastY - (float)ypos;
    lastX = (float)xpos;
    lastY = (float)ypos;

    gCamera.rotacionar(xoff, yoff);
}

// =========================================================
//  scroll_callback
// =========================================================
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset)
{
    gCamera.zoom((float)yoffset);
}

// =========================================================
//  salvarWaypoints — grava todos os waypoints em arquivo texto
// =========================================================
void salvarWaypoints(const vector<OBJModel>& objects)
{
    ofstream f("waypoints.txt");
    if (!f.is_open()) { cerr << "Erro ao salvar waypoints.txt" << endl; return; }

    for (int i = 0; i < (int)objects.size(); i++) {
        f << "object " << objects[i].name << "\n";
        for (auto& wp : objects[i].traj.waypoints)
            f << "  waypoint " << wp.x << " " << wp.y << " " << wp.z << "\n";
    }
    cout << "Waypoints salvos em waypoints.txt" << endl;
}

// =========================================================
//  Envia as 3 luzes ao fragment shader
// =========================================================
void sendLightUniforms(GLuint shader)
{
    char buf[64];
    for (int i = 0; i < 3; i++) {
        auto ul = [&](const char* member) -> GLint {
            snprintf(buf, sizeof(buf), "lights[%d].%s", i, member);
            return glGetUniformLocation(shader, buf);
        };
        glUniform3fv(ul("position"),  1, value_ptr(gLights[i].position));
        glUniform3fv(ul("color"),     1, value_ptr(gLights[i].color));
        glUniform1f (ul("intensity"),    gLights[i].intensity);
        glUniform1i (ul("enabled"),   gLights[i].enabled ? 1 : 0);
        glUniform1f (ul("constant"),     gLights[i].constant);
        glUniform1f (ul("linear"),       gLights[i].linear);
        glUniform1f (ul("quadratic"),    gLights[i].quadratic);
    }
}

// =========================================================
//  setupShader
// =========================================================
int setupShader()
{
    GLint ok; GLchar log[512];

    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &vertexShaderSource, NULL);
    glCompileShader(vs);
    glGetShaderiv(vs, GL_COMPILE_STATUS, &ok);
    if (!ok) { glGetShaderInfoLog(vs, 512, NULL, log); cerr << "VS:\n" << log; }

    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &fragmentShaderSource, NULL);
    glCompileShader(fs);
    glGetShaderiv(fs, GL_COMPILE_STATUS, &ok);
    if (!ok) { glGetShaderInfoLog(fs, 512, NULL, log); cerr << "FS:\n" << log; }

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) { glGetProgramInfoLog(prog, 512, NULL, log); cerr << "LINK:\n" << log; }

    glDeleteShader(vs);
    glDeleteShader(fs);
    return prog;
}

// =========================================================
//  loadTexture
// =========================================================
GLuint loadTexture(const string& filePath)
{
    GLuint texID;
    glGenTextures(1, &texID);
    glBindTexture(GL_TEXTURE_2D, texID);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    int width, height, nrCh;
    stbi_set_flip_vertically_on_load(true);
    unsigned char* data = stbi_load(filePath.c_str(), &width, &height, &nrCh, 0);
    if (data) {
        GLenum fmt = (nrCh == 4) ? GL_RGBA : GL_RGB;
        glTexImage2D(GL_TEXTURE_2D, 0, fmt, width, height, 0, fmt, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);
        cout << "Textura: " << filePath << " (" << width << "x" << height << ")" << endl;
    } else {
        cerr << "Falha ao carregar textura: " << filePath << endl;
        glDeleteTextures(1, &texID);
        texID = 0;
    }
    stbi_image_free(data);
    glBindTexture(GL_TEXTURE_2D, 0);
    return texID;
}

// =========================================================
//  loadMTL
// =========================================================
string loadMTL(const string& mtlPath)
{
    ifstream f(mtlPath);
    if (!f.is_open()) { cerr << "MTL nao encontrado: " << mtlPath << endl; return ""; }
    string line;
    while (getline(f, line)) {
        istringstream ss(line); string kw; ss >> kw;
        if (kw == "map_Kd") { string name; ss >> name; return name; }
    }
    return "";
}

// =========================================================
//  loadOBJWithNormals
//  Buffer: pos(3) + color(3) + normal(3) + uv(2) = 11 floats/vértice
// =========================================================
GLuint loadOBJWithNormals(const string& filePath, int& nVertices,
                           GLuint& texID, vec3 fallbackColor)
{
    texID = 0;
    vector<vec3>    positions, normals;
    vector<vec2>    texCoords;
    vector<GLfloat> vBuffer;

    string dir, mtlFile;
    size_t slash = filePath.find_last_of("/\\");
    if (slash != string::npos) dir = filePath.substr(0, slash + 1);

    ifstream file(filePath);
    if (!file.is_open()) { cerr << "Erro ao abrir: " << filePath << endl; return 0; }

    string line;
    while (getline(file, line)) {
        istringstream ss(line); string ww; ss >> ww;

        if      (ww == "mtllib") { ss >> mtlFile; }
        else if (ww == "v")  { vec3 v; ss>>v.x>>v.y>>v.z; positions.push_back(v); }
        else if (ww == "vt") { vec2 t; ss>>t.s>>t.t;      texCoords.push_back(t); }
        else if (ww == "vn") { vec3 n; ss>>n.x>>n.y>>n.z; normals.push_back(n);   }
        else if (ww == "f")  {
            struct FV { int vi, ti, ni; };
            vector<FV> fv;
            while (ss >> ww) {
                FV fface{0,-1,-1};
                istringstream face(ww); string idx;
                if (getline(face,idx,'/')) fface.vi = idx.empty()?0:stoi(idx)-1;
                if (getline(face,idx,'/')) fface.ti = idx.empty()?-1:stoi(idx)-1;
                if (getline(face,idx))     fface.ni = idx.empty()?-1:stoi(idx)-1;
                fv.push_back(fface);
            }
            for (int k=1; k+1<(int)fv.size(); k++) {
                for (int t : {0,k,k+1}) {
                    int vi=fv[t].vi, ti=fv[t].ti, ni=fv[t].ni;
                    auto& p = positions[vi];
                    vBuffer.insert(vBuffer.end(), {p.x,p.y,p.z,
                        fallbackColor.r,fallbackColor.g,fallbackColor.b});
                    if (ni>=0&&ni<(int)normals.size()) {
                        auto& n=normals[ni];
                        vBuffer.insert(vBuffer.end(),{n.x,n.y,n.z});
                    } else { vBuffer.insert(vBuffer.end(),{0.f,1.f,0.f}); }
                    if (ti>=0&&ti<(int)texCoords.size()) {
                        auto& tc=texCoords[ti];
                        vBuffer.insert(vBuffer.end(),{tc.s,tc.t});
                    } else { vBuffer.insert(vBuffer.end(),{0.f,0.f}); }
                }
            }
        }
    }
    file.close();

    if (!mtlFile.empty()) {
        string texName = loadMTL(dir + mtlFile);
        if (!texName.empty()) texID = loadTexture(dir + texName);
    }

    nVertices = (int)vBuffer.size() / 11;
    cout << "OBJ: " << filePath << "  (" << nVertices << " verts"
         << (texID ? ", textura" : "") << ")" << endl;

    GLuint VBO, VAO;
    glGenBuffers(1, &VBO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, vBuffer.size()*sizeof(GLfloat), vBuffer.data(), GL_STATIC_DRAW);
    glGenVertexArrays(1, &VAO);
    glBindVertexArray(VAO);
    const GLsizei S = 11*sizeof(GLfloat);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,S,(GLvoid*)0);                   glEnableVertexAttribArray(0);
    glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,S,(GLvoid*)(3*sizeof(GLfloat))); glEnableVertexAttribArray(1);
    glVertexAttribPointer(2,3,GL_FLOAT,GL_FALSE,S,(GLvoid*)(6*sizeof(GLfloat))); glEnableVertexAttribArray(2);
    glVertexAttribPointer(3,2,GL_FLOAT,GL_FALSE,S,(GLvoid*)(9*sizeof(GLfloat))); glEnableVertexAttribArray(3);
    glBindBuffer(GL_ARRAY_BUFFER,0); glBindVertexArray(0);
    return VAO;
}

// =========================================================
//  generateSphereSimple
// =========================================================
GLuint generateSphereSimple(float radius, int latSeg, int lonSeg,
                             int& nVertices, vec3 color)
{
    vector<GLfloat> vBuffer;
    auto push = [&](vec3 pos) {
        vec3 n = normalize(pos);
        vBuffer.insert(vBuffer.end(),
            {pos.x,pos.y,pos.z, color.r,color.g,color.b, n.x,n.y,n.z, 0.f,0.f});
    };
    auto calc = [&](int lat, int lon) -> vec3 {
        float theta = lat * pi<float>() / latSeg;
        float phi   = lon * 2.f * pi<float>() / lonSeg;
        return vec3(radius*cos(phi)*sin(theta), radius*cos(theta), radius*sin(phi)*sin(theta));
    };
    for (int i=0;i<latSeg;i++) for (int j=0;j<lonSeg;j++) {
        vec3 v0=calc(i,j),v1=calc(i+1,j),v2=calc(i,j+1),v3=calc(i+1,j+1);
        push(v0);push(v1);push(v2);
        push(v1);push(v3);push(v2);
    }
    nVertices = (int)vBuffer.size()/11;
    GLuint VBO,VAO;
    glGenBuffers(1,&VBO); glBindBuffer(GL_ARRAY_BUFFER,VBO);
    glBufferData(GL_ARRAY_BUFFER,vBuffer.size()*sizeof(GLfloat),vBuffer.data(),GL_STATIC_DRAW);
    glGenVertexArrays(1,&VAO); glBindVertexArray(VAO);
    const GLsizei S=11*sizeof(GLfloat);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,S,(GLvoid*)0);                   glEnableVertexAttribArray(0);
    glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,S,(GLvoid*)(3*sizeof(GLfloat))); glEnableVertexAttribArray(1);
    glVertexAttribPointer(2,3,GL_FLOAT,GL_FALSE,S,(GLvoid*)(6*sizeof(GLfloat))); glEnableVertexAttribArray(2);
    glVertexAttribPointer(3,2,GL_FLOAT,GL_FALSE,S,(GLvoid*)(9*sizeof(GLfloat))); glEnableVertexAttribArray(3);
    glBindBuffer(GL_ARRAY_BUFFER,0); glBindVertexArray(0);
    return VAO;
}
