/* DesafioM5 - Câmera em Primeira Pessoa
 *
 * Módulo 5 - Computação Gráfica - Unisinos
 * Autor: Kevin de Azevedo Garcia
 *
 * Implementa uma câmera FPS (first-person shooter) encapsulada na classe
 * Camera, que agrupa todos os atributos (posição, vetores de orientação,
 * ângulos de Euler) e expõe apenas as ações mover() e rotacionar().
 *
 * A câmera é construída sobre a matriz LookAt do GLM. O sistema de eixos
 * local da câmera (right, up, front) é recalculado a cada mudança de yaw
 * ou pitch a partir dos ângulos de Euler, mantendo os vetores ortonormais.
 *
 * O movimento usa delta time para ser independente do framerate.
 * O mouse controla o olhar; o teclado controla a posição.
 *
 * Cena: Suzanne + dois Cubos dispostos no espaço, com iluminação de três
 * pontos (key / fill / back) herdada do M4 e textura via MTL/stb_image.
 *
 * Controles:
 *   W / A / S / D     : mover câmera (frente/esquerda/trás/direita)
 *   Espaço / LShift   : subir / descer (modo voar)
 *   Mouse             : olhar ao redor (yaw + pitch)
 *   M                 : capturar / liberar cursor
 *   1 / 2 / 3         : ligar / desligar Key / Fill / Back light
 *   ESC               : sair
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
//
//  Buffer layout por vértice (11 floats):
//    location 0 : posição  (x, y, z)     offset  0
//    location 1 : cor      (r, g, b)     offset  3
//    location 2 : normal   (nx, ny, nz)  offset  6
//    location 3 : texcoord (s, t)        offset  9
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
    // Inverso-transposto: mantém normais corretas sob escala não-uniforme
    vNormal        = mat3(transpose(inverse(model))) * normal;
    vColor         = color;
    fragTexCoord   = texCoord;
}
)";

// =========================================================
//  Fragment Shader — Phong com 3 luzes pontuais + textura
//  Atenuação aplicada apenas na parcela difusa.
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

        // Atenuação — apenas no difuso
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
//  Classe Camera
//
//  Encapsula todos os atributos de uma câmera FPS:
//    - position  : posição no espaço mundo
//    - front/right/up : base ortonormal do referencial da câmera
//    - yaw / pitch    : ângulos de Euler (em graus)
//    - speed / sensitivity / fov : parâmetros de controle
//
//  Ações públicas:
//    mover(direcao, deltaTime) : desloca position ao longo dos eixos locais
//    rotacionar(xoff, yoff)    : atualiza yaw/pitch e reconstrói a base
//    getViewMatrix()           : retorna lookAt(position, position+front, up)
// =========================================================
enum CameraDirection { FORWARD, BACKWARD, LEFT, RIGHT, UP, DOWN };

class Camera
{
public:
    // --- Estado ---
    vec3  position;
    vec3  front;
    vec3  up;
    vec3  right;
    vec3  worldUp;

    // --- Ângulos de Euler (graus) ---
    float yaw;    // rotação horizontal (em torno de Y)
    float pitch;  // rotação vertical   (em torno de X)

    // --- Parâmetros de controle ---
    float speed;        // unidades/segundo
    float sensitivity;  // graus por pixel
    float fov;          // field of view (graus)

    // -------------------------------------------------------
    Camera(vec3  position  = vec3(0.0f, 0.5f, 5.0f),
           vec3  worldUp   = vec3(0.0f, 1.0f, 0.0f),
           float yaw       = -90.0f,
           float pitch     = 0.0f)
        : position(position),
          worldUp(worldUp),
          yaw(yaw),
          pitch(pitch),
          speed(3.5f),
          sensitivity(0.10f),
          fov(45.0f)
    {
        atualizarVetores();
    }

    // Retorna a matriz de visão (View) para o frame atual
    mat4 getViewMatrix() const
    {
        return lookAt(position, position + front, up);
    }

    // -------------------------------------------------------
    // mover: desloca a câmera ao longo do referencial local
    //   Usar worldUp para subir/descer garante que o jogador
    //   se move verticalmente mesmo olhando para baixo.
    // -------------------------------------------------------
    void mover(CameraDirection dir, float deltaTime)
    {
        float vel = speed * deltaTime;
        switch (dir) {
            case FORWARD:  position += front    * vel; break;
            case BACKWARD: position -= front    * vel; break;
            case LEFT:     position -= right    * vel; break;
            case RIGHT:    position += right    * vel; break;
            case UP:       position += worldUp  * vel; break;
            case DOWN:     position -= worldUp  * vel; break;
        }
    }

    // -------------------------------------------------------
    // rotacionar: recebe deslocamento do mouse em pixels e
    //   atualiza os ângulos de Euler, depois reconstrói a base.
    //   O pitch é limitado a ±89° para evitar o gimbal flip.
    // -------------------------------------------------------
    void rotacionar(float xoffset, float yoffset)
    {
        yaw   += xoffset * sensitivity;
        pitch += yoffset * sensitivity;

        if (pitch >  89.0f) pitch =  89.0f;
        if (pitch < -89.0f) pitch = -89.0f;

        atualizarVetores();
    }

    // -------------------------------------------------------
    // zoom via scroll do mouse
    // -------------------------------------------------------
    void zoom(float yoffset)
    {
        fov -= yoffset;
        if (fov <  5.0f) fov =  5.0f;
        if (fov > 90.0f) fov = 90.0f;
    }

private:
    // Reconstrói front, right e up a partir de yaw e pitch.
    // Equações de Euler para um sistema de câmera FPS:
    //   front.x = cos(yaw) * cos(pitch)
    //   front.y = sin(pitch)
    //   front.z = sin(yaw) * cos(pitch)
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
//  Structs auxiliares
// =========================================================
struct OBJModel
{
    GLuint VAO       = 0;
    int    nVertices = 0;
    GLuint texID     = 0;
    vec3   position  = vec3(0.0f);
    float  scale     = 1.0f;
    vec3   color     = vec3(0.8f, 0.7f, 0.6f);
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

// Estado do mouse
float lastX      = WIDTH  / 2.0f;
float lastY      = HEIGHT / 2.0f;
bool  firstMouse = true;
bool  mouseCaptured = true;

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

// =========================================================
//  main
// =========================================================
int main()
{
    glfwInit();
    GLFWwindow* window = glfwCreateWindow(WIDTH, HEIGHT,
        "DesafioM5 - Camera FPS -- Kevin Garcia", nullptr, nullptr);
    glfwMakeContextCurrent(window);

    // Registrar callbacks
    glfwSetKeyCallback(window, key_callback);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetScrollCallback(window, scroll_callback);

    // Capturar o cursor (modo FPS)
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

    // --- Cena: Suzanne + dois Cubos ---
    vector<OBJModel> objects;

    // Suzanne — modelo principal, ao centro
    {
        OBJModel m;
        m.color    = vec3(0.85f, 0.72f, 0.60f);
        m.position = vec3(0.0f, 0.0f, 0.0f);
        m.scale    = 1.0f;
        m.VAO      = loadOBJWithNormals("../assets/Modelos3D/Suzanne.obj",
                                        m.nVertices, m.texID, m.color);
        if (m.VAO) objects.push_back(m);
    }
    // Cubo — à direita e atrás
    {
        OBJModel m;
        m.color    = vec3(0.3f, 0.6f, 1.0f);
        m.position = vec3(3.5f, 0.0f,-2.0f);
        m.scale    = 1.2f;
        m.VAO      = loadOBJWithNormals("../assets/Modelos3D/Cube.obj",
                                        m.nVertices, m.texID, m.color);
        if (m.VAO) objects.push_back(m);
    }
    // Cubo — à esquerda e atrás
    {
        OBJModel m;
        m.color    = vec3(0.4f, 1.0f, 0.5f);
        m.position = vec3(-4.0f, 0.5f,-3.5f);
        m.scale    = 0.8f;
        m.VAO      = loadOBJWithNormals("../assets/Modelos3D/Cube.obj",
                                        m.nVertices, m.texID, m.color);
        if (m.VAO) objects.push_back(m);
    }

    if (objects.empty()) {
        cerr << "Nenhum modelo carregado. Encerrando." << endl;
        glfwTerminate();
        return -1;
    }

    // --- Iluminação de três pontos (posições fixas no mundo) ---
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
        sphereVAO[i] = generateSphereSimple(0.08f, 12, 12,
                                            nSphereVerts, gLights[i].color);

    // --- Uniforms que não mudam por frame ---
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

    cout << "\n=== Câmera FPS ===" << endl;
    cout << "W/A/S/D        : mover" << endl;
    cout << "Espaco/LShift  : subir / descer" << endl;
    cout << "Mouse          : olhar ao redor" << endl;
    cout << "M              : capturar / liberar cursor" << endl;
    cout << "1 / 2 / 3      : toggle Key / Fill / Back light" << endl;
    cout << "Scroll         : zoom (FOV)" << endl;
    cout << "ESC            : sair" << endl;
    cout << "\nPosicao inicial: " << to_string(gCamera.position.x)
         << ", " << to_string(gCamera.position.y)
         << ", " << to_string(gCamera.position.z) << endl;

    // --- Delta time ---
    float lastFrame = 0.0f;

    // =========================================================
    //  Loop principal
    // =========================================================
    while (!glfwWindowShouldClose(window))
    {
        // Delta time
        float currentFrame = (float)glfwGetTime();
        float deltaTime    = currentFrame - lastFrame;
        lastFrame          = currentFrame;

        glfwPollEvents();

        // --- Movimento contínuo com glfwGetKey (suave com delta time) ---
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

        glClearColor(0.05f, 0.05f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // View e projection se atualizam a cada frame pois a câmera se move
        mat4 view = gCamera.getViewMatrix();
        mat4 proj = perspective(radians(gCamera.fov),
                                (float)WIDTH / HEIGHT, 0.1f, 100.0f);

        glUniformMatrix4fv(viewLoc, 1, GL_FALSE, value_ptr(view));
        glUniformMatrix4fv(projLoc, 1, GL_FALSE, value_ptr(proj));
        glUniform3fv(camLoc, 1, value_ptr(gCamera.position));

        sendLightUniforms(shaderID);

        // ---------------------------------------------------------
        //  Desenhar objetos da cena com Phong completo
        // ---------------------------------------------------------
        glUniform1f(kaLoc, 0.10f);
        glUniform1f(kdLoc, 0.70f);
        glUniform1f(ksLoc, 0.50f);

        for (auto& obj : objects)
        {
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
        //  Marcadores de luz (esferas emissivas — ka=1, kd=ks=0)
        // ---------------------------------------------------------
        glUniform1i(useTextureLoc, 0);
        glUniform1f(kaLoc, 1.0f);
        glUniform1f(kdLoc, 0.0f);
        glUniform1f(ksLoc, 0.0f);

        for (int i = 0; i < 3; i++)
        {
            if (!gLights[i].enabled) continue;
            mat4 model = translate(mat4(1.0f), gLights[i].position);
            model      = scale(model, vec3(1.0f)); // já têm o raio na geração
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, value_ptr(model));
            glBindVertexArray(sphereVAO[i]);
            glDrawArrays(GL_TRIANGLES, 0, nSphereVerts);
            glBindVertexArray(0);
        }

        glfwSwapBuffers(window);
    }

    for (auto& obj : objects) {
        glDeleteVertexArrays(1, &obj.VAO);
        if (obj.texID) glDeleteTextures(1, &obj.texID);
    }
    for (int i = 0; i < 3; i++) glDeleteVertexArrays(1, &sphereVAO[i]);
    glfwTerminate();
    return 0;
}

// =========================================================
//  key_callback — eventos discretos (não contínuos)
//  O movimento contínuo (WASD) é tratado no loop com glfwGetKey.
// =========================================================
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mode)
{
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, GL_TRUE);
        return;
    }

    // Alternar captura do mouse
    if (key == GLFW_KEY_M && action == GLFW_PRESS) {
        mouseCaptured = !mouseCaptured;
        firstMouse    = true; // evitar salto de posição ao recapturar
        glfwSetInputMode(window, GLFW_CURSOR,
            mouseCaptured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
        cout << "Cursor: " << (mouseCaptured ? "capturado" : "livre") << endl;
    }

    // Toggle das luzes
    auto toggleLight = [&](int idx) {
        gLights[idx].enabled = !gLights[idx].enabled;
        cout << gLights[idx].name << ": "
             << (gLights[idx].enabled ? "LIGADA" : "DESLIGADA") << endl;
    };
    if (key == GLFW_KEY_1 && action == GLFW_PRESS) toggleLight(0);
    if (key == GLFW_KEY_2 && action == GLFW_PRESS) toggleLight(1);
    if (key == GLFW_KEY_3 && action == GLFW_PRESS) toggleLight(2);
}

// =========================================================
//  mouse_callback — delega à Camera::rotacionar()
//
//  GLFW entrega xpos/ypos em pixels (origem no canto superior-esquerdo).
//  O eixo Y é invertido antes de passar à câmera porque, na tela, Y
//  cresce para baixo, mas o pitch deve crescer para cima.
// =========================================================
void mouse_callback(GLFWwindow* window, double xpos, double ypos)
{
    if (!mouseCaptured) return;

    // Na primeira chamada, inicializa a última posição para evitar salto
    if (firstMouse) {
        lastX = (float)xpos;
        lastY = (float)ypos;
        firstMouse = false;
    }

    float xoffset =  (float)xpos - lastX;
    float yoffset =  lastY - (float)ypos; // Y invertido: tela↓ = câmera↑
    lastX = (float)xpos;
    lastY = (float)ypos;

    gCamera.rotacionar(xoffset, yoffset);
}

// =========================================================
//  scroll_callback — zoom via FOV
// =========================================================
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset)
{
    gCamera.zoom((float)yoffset);
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
//  loadTexture — carrega PNG/JPG com stb_image
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
//  loadMTL — extrai nome de textura difusa (map_Kd) do MTL
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
        istringstream ss(line); string w; ss >> w;

        if      (w == "mtllib") { ss >> mtlFile; }
        else if (w == "v")  { vec3 v; ss>>v.x>>v.y>>v.z; positions.push_back(v); }
        else if (w == "vt") { vec2 t; ss>>t.s>>t.t;      texCoords.push_back(t); }
        else if (w == "vn") { vec3 n; ss>>n.x>>n.y>>n.z; normals.push_back(n);   }
        else if (w == "f")  {
            struct FV { int vi, ti, ni; };
            vector<FV> fv;
            while (ss >> w) {
                FV f{0,-1,-1};
                istringstream face(w); string idx;
                if (getline(face,idx,'/')) f.vi = idx.empty()?0:stoi(idx)-1;
                if (getline(face,idx,'/')) f.ti = idx.empty()?-1:stoi(idx)-1;
                if (getline(face,idx))     f.ni = idx.empty()?-1:stoi(idx)-1;
                fv.push_back(f);
            }
            for (int k=1; k+1<(int)fv.size(); k++) {
                for (int t : {0,k,k+1}) {
                    int vi=fv[t].vi, ti=fv[t].ti, ni=fv[t].ni;
                    auto& p = positions[vi];
                    vBuffer.insert(vBuffer.end(), {p.x,p.y,p.z,
                        fallbackColor.r, fallbackColor.g, fallbackColor.b});
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
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,S,(GLvoid*)0);              glEnableVertexAttribArray(0);
    glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,S,(GLvoid*)(3*sizeof(GLfloat))); glEnableVertexAttribArray(1);
    glVertexAttribPointer(2,3,GL_FLOAT,GL_FALSE,S,(GLvoid*)(6*sizeof(GLfloat))); glEnableVertexAttribArray(2);
    glVertexAttribPointer(3,2,GL_FLOAT,GL_FALSE,S,(GLvoid*)(9*sizeof(GLfloat))); glEnableVertexAttribArray(3);
    glBindBuffer(GL_ARRAY_BUFFER,0); glBindVertexArray(0);
    return VAO;
}

// =========================================================
//  generateSphereSimple — esfera procedural (marcador de luz)
//  Buffer: pos(3)+color(3)+normal(3)+uv(2) = 11 floats/vértice
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
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,S,(GLvoid*)0);              glEnableVertexAttribArray(0);
    glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,S,(GLvoid*)(3*sizeof(GLfloat))); glEnableVertexAttribArray(1);
    glVertexAttribPointer(2,3,GL_FLOAT,GL_FALSE,S,(GLvoid*)(6*sizeof(GLfloat))); glEnableVertexAttribArray(2);
    glVertexAttribPointer(3,2,GL_FLOAT,GL_FALSE,S,(GLvoid*)(9*sizeof(GLfloat))); glEnableVertexAttribArray(3);
    glBindBuffer(GL_ARRAY_BUFFER,0); glBindVertexArray(0);
    return VAO;
}
