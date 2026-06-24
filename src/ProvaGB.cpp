// Forca uso da GPU dedicada NVIDIA em sistemas com GPU dupla (notebooks)
extern "C" { __declspec(dllexport) unsigned long NvOptimusEnablement = 1; }
extern "C" { __declspec(dllexport) unsigned long AmdPowerXpressRequestHighPerformance = 1; }

/* ProvaGB.cpp - Demonstracao Tecnica Final
 *
 * Computacao Grafica - Unisinos
 * Autor: Kevin de Azevedo Garcia
 *
 * Hierarquia de cena:
 *   theatrical_drag_show  (root / palco)
 *   pillar                (centro do palco)
 *   rupaul                (sobre o pillar, animada com Z/X)
 *   microphone            (ao lado da rupaul, animado em curva de Bezier)
 *   camera_1 / camera_2   (orbitais 360, animadas com sin/cos)
 *   lustre x3             (no teto, sao as 3 fontes de luz principais)
 *   foto x3               (quads com fotos da RuPaul na parede do fundo)
 *   letreiro              (parede do fundo, acima das fotos)
 *   batom                 (ao lado da RuPaul, controlavel pelo usuario)
 *
 * Luzes (teclas 1-8):
 *   1 : Key Light   — lustre central (luz quente principal)
 *   2 : Fill Light  — lustre esquerda (luz fria, preenche sombras)
 *   3 : Back Light  — lustre direita (separa sujeito do fundo)
 *   4 : Spotlight camera_1 (segue a camera em orbita)
 *   5 : Spotlight camera_2 (segue a camera em orbita)
 *   6 : Face top    — luz de rosto superior
 *   7 : Face left   — luz de rosto esquerda
 *   8 : Face right  — luz de rosto direita
 *
 * Animacoes:
 *   SPACE : pause/resume microfone (curva de Bezier cubica)
 *   P     : pause/resume orbita das cameras (sin/cos)
 *
 * Camera:
 *   F                              : alterna Orbital <-> FPS
 *   Orbital: mouse drag + scroll   : rotacionar / zoom
 *   FPS: W/A/S/D                   : mover   Q/E: subir/descer
 *        mouse                     : olhar ao redor
 *
 * Objetos:
 *   Z / X        : rotacionar RuPaul no proprio eixo Y
 *   Setas / Y / U: mover batom (X/Z/Y)
 *   C / V        : rotacionar batom no eixo Y
 *   B / N        : escala uniforme do batom
 *   T            : toggle textura global (useTexture 0=cor / 1=.mtl)
 *   L            : listar estado atual de todas as luzes no console
 *   ESC          : sair
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
//  -- MULTIPLICACAO DE MATRIZES: Model * View * Projection --
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
    // MULTIPLICACAO MVP: objeto -> mundo -> clip space
    vec4 worldPos  = model * vec4(position, 1.0);      // Model: objeto -> mundo
    gl_Position    = projection * view * worldPos;     // View*Proj: mundo -> clip
    fragPos        = vec3(worldPos);
    vNormal        = mat3(transpose(inverse(model))) * normal;
    vColor         = color;
    fragTexCoord   = texCoord;
}
)";

// =========================================================
//  Fragment Shader
//
//  MODELO DE ILUMINACAO PHONG:
//    resultado = ambiente + difusa + especular
//    - Ambiente : ka * cor_objeto
//    - Difusa   : kd * max(dot(N,L), 0) * cor_luz * intensidade * atenuacao
//    - Especular: ks * pow(max(dot(R,V), 0), q) * cor_luz * intensidade
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

uniform PointLight lights[9];
uniform sampler2D  texBuff;
uniform int        useTexture;
uniform vec3       camPos;
uniform float      ka;
uniform float      kd;
uniform float      ks;
uniform float      q;

void main()
{
    // Cor base: textura ou cor de fallback
    vec3 objColor = (useTexture == 1)
                    ? vec3(texture(texBuff, fragTexCoord))
                    : vColor;

    vec3 N = normalize(vNormal);
    vec3 V = normalize(camPos - fragPos);  // vetor ao observador

    // PHONG - componente ambiente (luz global uniforme)
    vec3 result = ka * objColor;

    for (int i = 0; i < 9; i++)
    {
        if (lights[i].enabled == 0) continue;

        vec3  L    = normalize(lights[i].position - fragPos);  // vetor a luz
        float dist = length(lights[i].position - fragPos);

        // Atenuacao por distancia
        float att = 1.0 / (lights[i].constant
                         + lights[i].linear    * dist
                         + lights[i].quadratic * dist * dist);

        // PHONG - componente difusa (Lei de Lambert)
        float diff    = max(dot(N, L), 0.0);
        vec3  diffuse = kd * diff * lights[i].color * lights[i].intensity * att;

        // PHONG - componente especular (reflexo de Phong)
        vec3  R       = normalize(reflect(-L, N));
        float spec    = pow(max(dot(R, V), 0.0), q);
        vec3  specular = ks * spec * lights[i].color * lights[i].intensity;

        result += diffuse * objColor + specular;
    }

    color = vec4(result, 1.0);
}
)";

// =========================================================
//  Struct: luz pontual
// =========================================================
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
//  Struct: objeto OBJ da cena
// =========================================================
struct OBJModel
{
    GLuint VAO        = 0;
    int    nVertices  = 0;
    GLuint texID      = 0;
    vec3   position   = vec3(0.0f);
    vec3   rotation   = vec3(0.0f);  // angulos Euler em graus (X, Y, Z)
    float  scale      = 1.0f;
    vec3   color      = vec3(0.8f, 0.7f, 0.6f);
    string name;
    bool   useTexture = true;
};

// =========================================================
//  Funcao Bezier cubica
//  Interpola uma posicao ao longo de uma curva definida por
//  4 pontos de controle. t em [0,1].
// =========================================================
vec3 bezier(float t, vec3 p0, vec3 p1, vec3 p2, vec3 p3)
{
    float u  = 1.0f - t;
    float u2 = u * u;
    float u3 = u2 * u;
    float t2 = t * t;
    float t3 = t2 * t;
    return u3*p0 + 3.0f*u2*t*p1 + 3.0f*u*t2*p2 + t3*p3;
}

// =========================================================
//  Camera FPS
// =========================================================
enum CameraDirection { FORWARD, BACKWARD, LEFT, RIGHT, UP, DOWN };

class Camera
{
public:
    vec3  position  = vec3(0.0f, -3.0f, 8.0f);
    vec3  front, up, right, worldUp;
    float yaw = -90.0f, pitch = 0.0f;
    float speed = 5.0f, sensitivity = 0.12f, fov = 45.0f;

    Camera() : worldUp(vec3(0,1,0)) { update(); }

    mat4 getView() const { return lookAt(position, position + front, up); }

    void mover(CameraDirection d, float dt) {
        float v = speed * dt;
        if (d == FORWARD)  position += front   * v;
        if (d == BACKWARD) position -= front   * v;
        if (d == LEFT)     position -= right   * v;
        if (d == RIGHT)    position += right   * v;
        if (d == UP)       position += worldUp * v;
        if (d == DOWN)     position -= worldUp * v;
    }

    void rotacionar(float dx, float dy) {
        yaw   += dx * sensitivity;
        pitch += dy * sensitivity;
        if (pitch >  89.0f) pitch =  89.0f;
        if (pitch < -89.0f) pitch = -89.0f;
        update();
    }

    void zoom(float y) { fov -= y; fov = clamp(fov, 5.0f, 90.0f); }

private:
    void update() {
        vec3 f;
        f.x = cos(radians(yaw)) * cos(radians(pitch));
        f.y = sin(radians(pitch));
        f.z = sin(radians(yaw)) * cos(radians(pitch));
        front = normalize(f);
        right = normalize(cross(front, worldUp));
        up    = normalize(cross(right, front));
    }
};

// =========================================================
//  Globals
// =========================================================
const GLuint WIDTH = 1920, HEIGHT = 1080;

PointLight gLights[9];

// Camera orbital de visualizacao
float gCamRadius = 18.0f;
float gCamYaw    = -90.0f;
float gCamPitch  =  20.0f;
bool  gMouseDown = false;
float gLastX     = WIDTH  / 2.0f;
float gLastY     = HEIGHT / 2.0f;

// Camera FPS
Camera    gFPSCam;
bool      gFPSMode    = false;  // F: alterna orbital <-> FPS
bool      gFirstMouse = true;

bool gLightStatus[9] = { true, true, true, false, true, true, true, true, true };

bool gAnimating     = true;  // microfone (Bezier) — SPACE
bool gCamsAnimating = true;  // cameras orbitais  — P

vector<OBJModel>* gObjects = nullptr;

// =========================================================
//  Prototipos
// =========================================================
void   key_callback(GLFWwindow*, int, int, int, int);
void   mouse_button_callback(GLFWwindow*, int, int, int);
void   mouse_callback(GLFWwindow*, double, double);
void   scroll_callback(GLFWwindow*, double, double);
int    setupShader();
GLuint loadTexture(const string&);
string loadMTL(const string&);
GLuint loadOBJWithNormals(const string&, int&, GLuint&, vec3);
void   sendLightUniforms(GLuint, const bool lightStatus[9]);
GLuint generateQuad(float w, float h, int& nVertices);
void   printControls(const vector<OBJModel>&);

// =========================================================
//  main
// =========================================================
int main()
{
    glfwInit();
    GLFWwindow* window = glfwCreateWindow(WIDTH, HEIGHT,
        "ProvaGB - Demonstracao Tecnica -- Kevin Garcia", nullptr, nullptr);
    glfwMakeContextCurrent(window);

    glfwSetKeyCallback(window, key_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetScrollCallback(window, scroll_callback);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
        cout << "Falha ao inicializar GLAD" << endl;

    cout << "Renderer : " << glGetString(GL_RENDERER) << endl;
    cout << "OpenGL   : " << glGetString(GL_VERSION)  << endl;

    int fw, fh;
    glfwGetFramebufferSize(window, &fw, &fh);
    glViewport(0, 0, fw, fh);

    GLuint shaderID = setupShader();
    glUseProgram(shaderID);

    // =========================================================
    //  Cena — carregamento de modelos
    //  Caminho relativo a partir de src/ -> ../assets/Modelos3D/
    // =========================================================
    vector<OBJModel> objects;
    objects.reserve(11);

    const string BASE = "../assets/Modelos3D/";

    // [0] Palco (theatrical_drag_show) — root
    {
        OBJModel m;
        m.name     = "theatrical_drag_show";
        m.color    = vec3(0.9f, 0.85f, 0.8f);
        m.position = vec3(0.0f, -2.0f, -10.0f);  // fundo da cena, desenhado primeiro
        m.scale    = 15.0f;  // escala grande para funcionar como cenario/container
        m.VAO      = loadOBJWithNormals(
            BASE + "theatrical_drag_show/Meshy_AI_Half_open_diorama_of__0623214600_texture.obj",
            m.nVertices, m.texID, m.color);
        objects.push_back(m);
    }

    // [1] Pillar — centro do palco (0,0,0)
    {
        OBJModel m;
        m.name     = "pillar";
        m.color    = vec3(0.85f, 0.80f, 0.75f);
        m.position = vec3(0.0f, -8.5f, -7.0f);  // base no chao do palco
        m.scale    = 50.0f;
        m.VAO      = loadOBJWithNormals(
            BASE + "pillar/Meshy_AI_Morning_Glory_on_a_Pe_0623223642_texture.obj",
            m.nVertices, m.texID, m.color);
        objects.push_back(m);
    }

    // [2] RuPaul — sobre o pillar
    {
        OBJModel m;
        m.name     = "rupaul";
        m.color    = vec3(0.95f, 0.85f, 0.70f);
        m.position = vec3(0.0f, -5.2f, -7.2f);
        m.scale    = 1.0f;
        m.VAO      = loadOBJWithNormals(
            BASE + "rupaul/Meshy_AI_Glamour_in_Platinum_C_0623211816_texture.obj",
            m.nVertices, m.texID, m.color);
        objects.push_back(m);
    }

    // [3-6] Umbrellas de studio
    // {
    //     const vec3  uPos[4] = {
    //         vec3( 0.0f, -5.5f,  1.5f),
    //         vec3( 0.0f, -5.5f, -5.5f),
    //         vec3(-3.5f, -5.5f, -2.0f),
    //         vec3( 3.5f, -5.5f, -2.0f)
    //     };
    //     const float uRotY[4]   = { 180.0f, 0.0f, 90.0f, -90.0f };
    //     const string uLabel[4] = { "umbrella_frente", "umbrella_tras",
    //                                 "umbrella_esquerda", "umbrella_direita" };
    //     for (int i = 0; i < 4; i++) {
    //         OBJModel m;
    //         m.name     = uLabel[i];
    //         m.color    = vec3(0.95f, 0.95f, 0.95f);
    //         m.position = uPos[i];
    //         m.rotation = vec3(0.0f, uRotY[i], 0.0f);
    //         m.scale    = 1.0f;
    //         m.VAO      = loadOBJWithNormals(
    //             BASE + "umbrella/Meshy_AI_White_translucent_sho_0623201546_texture.obj",
    //             m.nVertices, m.texID, m.color);
    //         objects.push_back(m);
    //     }
    // }

    // [7] Sign — parede do fundo
    // {
    //     OBJModel m;
    //     m.name     = "sign";
    //     m.color    = vec3(1.0f, 0.9f, 0.2f);
    //     m.position = vec3(0.0f, 1.0f, -2.0f);
    //     m.scale    = 1.0f;
    //     m.VAO      = loadOBJWithNormals(
    //         BASE + "sign/Meshy_AI_Empty_marquee_sign_fr_0623220942_texture.obj",
    //         m.nVertices, m.texID, m.color);
    //     objects.push_back(m);
    // }

    // [8] Microphone — proximo ao busto, animado em Bezier
    {
        OBJModel m;
        m.name     = "microphone";
        m.color    = vec3(0.7f, 0.7f, 0.7f);
        m.position = vec3(-3.0f, -5.2f, -6.0f);  // a frente e ao lado da rupaul
        m.scale    = 0.5f;
        m.VAO      = loadOBJWithNormals(
            BASE + "microphone/Meshy_AI_Vintage_1950s_silver__0623212423_texture.obj",
            m.nVertices, m.texID, m.color);
        objects.push_back(m);
    }

    // [9-10] Cameras orbitais — animadas em Bezier
    {
        const vec3 camPos[2] = {
            vec3(-4.0f, -5.0f, -5.0f),   // esquerda
            vec3( 4.0f, -5.0f, -5.0f)    // direita
        };
        for (int i = 0; i < 2; i++) {
            OBJModel m;
            m.name     = (i == 0) ? "camera_1" : "camera_2";
            m.color    = vec3(0.2f, 0.2f, 0.2f);
            m.position = camPos[i];
            m.scale    = 0.8f;
            m.VAO      = loadOBJWithNormals(
                BASE + "camera/Meshy_AI_Professional_digital__0623204432_texture.obj",
                m.nVertices, m.texID, m.color);
            objects.push_back(m);
        }
    }

    // [6-8] Lustres no teto — Key Light (frente), Fill Light (esquerda), Back Light (direita)
    {
        const vec3 lustrePos[3] = {
            vec3( 0.0f, 2.0f, -5.0f),   // centro-frente (Key)
            vec3(-4.0f, 2.0f, -7.0f),   // esquerda      (Fill)
            vec3( 4.0f, 2.0f, -7.0f)    // direita       (Back)
        };
        for (int i = 0; i < 3; i++) {
            OBJModel m;
            m.name     = "lustre_" + to_string(i + 1);
            m.color    = vec3(0.9f, 0.85f, 0.7f);
            m.position = lustrePos[i];
            m.scale    = 1.0f;
            m.VAO      = loadOBJWithNormals(
                BASE + "lustres/Meshy_AI_Tiered_Crystal_Chande_0624010343_texture.obj",
                m.nVertices, m.texID, m.color);
            objects.push_back(m);
        }
    }

    // [9-11] Fotos da RuPaul na parede do fundo — quad plano sem moldura
    {
        const string fotoPath = BASE + "photography/fotos/";
        const string fotos[3] = { "rupaul1.png", "rupaul2.png", "rupaul3.png" };
        const vec3 fotoPos[3] = {
            vec3(-4.5f, -3.0f, -12.4f),  // esquerda
            vec3( 0.0f, -3.0f, -12.4f),  // centro
            vec3( 4.5f, -3.0f, -12.4f)   // direita
        };
        for (int i = 0; i < 3; i++) {
            OBJModel m;
            m.name       = "foto_" + to_string(i + 1);
            m.color      = vec3(1.0f, 1.0f, 1.0f);
            m.position   = fotoPos[i];
            m.scale      = 1.0f;
            m.useTexture = true;
            m.VAO        = generateQuad(3.0f, 4.0f, m.nVertices);  // largura x altura
            m.texID      = loadTexture(fotoPath + fotos[i]);
            objects.push_back(m);
        }
    }

    // Letreiro — parede do fundo, acima do quadro central
    {
        OBJModel m;
        m.name     = "letreiro";
        m.color    = vec3(1.0f, 0.9f, 0.2f);
        m.position = vec3(0.0f, 0.5f, -12.3f);
        m.scale    = 3.0f;
        m.VAO      = loadOBJWithNormals(
            BASE + "letreiro/Meshy_AI_dragrace_0624200741_texture.obj",
            m.nVertices, m.texID, m.color);
        objects.push_back(m);
    }

    // Batom — ao lado da RuPaul (que esta em 0, -5.2, -7.2)
    {
        OBJModel m;
        m.name     = "batom";
        m.color    = vec3(0.8f, 0.1f, 0.2f);
        m.position = vec3(2.0f, -5.2f, -7.0f);
        m.scale    = 1.0f;
        m.VAO      = loadOBJWithNormals(
            BASE + "batom/Meshy_AI_Classic_elegant_green_0624184455_texture.obj",
            m.nVertices, m.texID, m.color);
        objects.push_back(m);
    }

    if (objects.empty()) {
        cerr << "Nenhum modelo carregado. Encerrando." << endl;
        glfwTerminate();
        return -1;
    }
    gObjects = &objects;

    // =========================================================
    //  Iluminacao de tres pontos — luzes nas posicoes dos lustres
    // =========================================================
    gLights[0] = { vec3( 0.0f, 2.0f, -5.0f), vec3(1.00f, 0.95f, 0.80f),
                   2.0f, true, 1.0f, 0.07f, 0.017f, "Key Light  lustre_1 [1]" };
    gLights[1] = { vec3(-4.0f, 2.0f, -7.0f), vec3(0.70f, 0.85f, 1.00f),
                   1.5f, true, 1.0f, 0.07f, 0.017f, "Fill Light lustre_2 [2]" };
    gLights[2] = { vec3( 4.0f, 2.0f, -7.0f), vec3(0.90f, 0.90f, 1.00f),
                   1.5f, true, 1.0f, 0.07f, 0.017f, "Back Light lustre_3 [3]" };
    gLights[3] = { vec3( 0.0f, 0.0f,  0.0f), vec3(0.0f,  0.0f,  0.0f),
                   0.0f, false, 1.0f, 0.0f, 0.0f,   "desativada"              };
    // Luzes das cameras — posicao atualizada a cada frame no loop
    gLights[4] = { vec3(-4.0f, -5.0f, -5.0f), vec3(1.00f, 0.95f, 1.00f),
                   1.2f, true, 1.0f, 0.14f, 0.07f,  "camera_1 spotlight [4]"  };
    gLights[5] = { vec3( 4.0f, -5.0f, -5.0f), vec3(1.00f, 0.95f, 1.00f),
                   1.2f, true, 1.0f, 0.14f, 0.07f,  "camera_2 spotlight [5]"  };
    // Luzes de rosto — iluminam a face da RuPaul de perto (teclas 6/7/8)
    // RuPaul esta em (0, -5.2, -7.2); luzes posicionadas na frente e lados do rosto
    gLights[6] = { vec3( 0.0f, -2.5f, -5.5f), vec3(1.00f, 0.98f, 0.95f),
                   2.0f, true, 1.0f, 0.20f, 0.10f,  "face_top    [6]"         };
    gLights[7] = { vec3(-1.5f, -4.0f, -5.5f), vec3(1.00f, 0.95f, 0.90f),
                   1.5f, true, 1.0f, 0.20f, 0.10f,  "face_left   [7]"         };
    gLights[8] = { vec3( 1.5f, -4.0f, -5.5f), vec3(1.00f, 0.95f, 0.90f),
                   1.5f, true, 1.0f, 0.20f, 0.10f,  "face_right  [8]"         };

    // Uniform locations
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

    // Bezier do microphone: sobe e desce ao lado da rupaul
    vec3 micP0 = vec3( 0.8f, -5.0f, -6.0f);
    vec3 micP1 = vec3( 0.8f, -5.2f, -6.0f);
    vec3 micP2 = vec3( 0.8f, -5.2f, -6.0f);
    vec3 micP3 = vec3( 0.8f, -5.0f, -6.0f);

    // Orbita 360 das cameras: centro em torno da rupaul
    const vec3  camCenter = vec3(0.0f, -7.2f, -7.2f); // centro de orbita (X,Z da rupaul)
    const float camRadius = 5.5f;   // raio da orbita
    const float camSpeed  = 0.4f;   // radianos/segundo

    printControls(objects);

    float lastFrame  = 0.0f;
    float bezierTime = 0.0f;  // usado pelo microphone

    // =========================================================
    //  Loop principal
    // =========================================================
    while (!glfwWindowShouldClose(window))
    {
        float currentFrame = (float)glfwGetTime();
        float deltaTime    = currentFrame - lastFrame;
        lastFrame          = currentFrame;

        glfwPollEvents();

        // Movimento FPS (so ativo no modo FPS)
        if (gFPSMode) {
            if (glfwGetKey(window, GLFW_KEY_W)          == GLFW_PRESS) gFPSCam.mover(FORWARD,  deltaTime);
            if (glfwGetKey(window, GLFW_KEY_S)          == GLFW_PRESS) gFPSCam.mover(BACKWARD, deltaTime);
            if (glfwGetKey(window, GLFW_KEY_A)          == GLFW_PRESS) gFPSCam.mover(LEFT,     deltaTime);
            if (glfwGetKey(window, GLFW_KEY_D)          == GLFW_PRESS) gFPSCam.mover(RIGHT,    deltaTime);
            if (glfwGetKey(window, GLFW_KEY_Q)          == GLFW_PRESS) gFPSCam.mover(UP,       deltaTime);
            if (glfwGetKey(window, GLFW_KEY_E)          == GLFW_PRESS) gFPSCam.mover(DOWN,     deltaTime);
        }

        // Z / X: girar a RuPaul no proprio eixo Y (objeto index 2)
        if ((int)objects.size() > 2) {
            const float rSpd = 60.0f * deltaTime;
            if (glfwGetKey(window, GLFW_KEY_Z) == GLFW_PRESS)
                objects[2].rotation.y -= rSpd;
            if (glfwGetKey(window, GLFW_KEY_X) == GLFW_PRESS)
                objects[2].rotation.y += rSpd;
        }

        // Setas / Y / U: mover o batom (ultimo objeto)
        // C / V: rotacionar o batom no eixo Y
        // B / N: escala uniforme do batom
        if (!objects.empty()) {
            auto& batom = objects.back();
            const float tSpd = 2.0f  * deltaTime;
            const float rSpd = 60.0f * deltaTime;
            const float sSpd = 0.5f  * deltaTime;

            if (glfwGetKey(window, GLFW_KEY_LEFT)  == GLFW_PRESS) batom.position.x -= tSpd;
            if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) batom.position.x += tSpd;
            if (glfwGetKey(window, GLFW_KEY_UP)    == GLFW_PRESS) batom.position.z -= tSpd;
            if (glfwGetKey(window, GLFW_KEY_DOWN)  == GLFW_PRESS) batom.position.z += tSpd;
            if (glfwGetKey(window, GLFW_KEY_Y)     == GLFW_PRESS) batom.position.y += tSpd;
            if (glfwGetKey(window, GLFW_KEY_U)     == GLFW_PRESS) batom.position.y -= tSpd;

            if (glfwGetKey(window, GLFW_KEY_C)     == GLFW_PRESS) batom.rotation.y -= rSpd;
            if (glfwGetKey(window, GLFW_KEY_V)     == GLFW_PRESS) batom.rotation.y += rSpd;

            if (glfwGetKey(window, GLFW_KEY_B)     == GLFW_PRESS) batom.scale += sSpd;
            if (glfwGetKey(window, GLFW_KEY_N)     == GLFW_PRESS) {
                batom.scale -= sSpd;
                if (batom.scale < 0.05f) batom.scale = 0.05f;
            }
        }

        // Microphone: Bezier ping-pong independente
        if (gAnimating) {
            bezierTime += deltaTime * 0.1f;
            float traw = fmod(bezierTime, 2.0f);
            float t    = (traw > 1.0f) ? (2.0f - traw) : traw;
            if ((int)objects.size() > 3)
                objects[3].position = bezier(t, micP0, micP1, micP2, micP3);
        }

        // Cameras: orbita circular — pausavel com SPACE
        if (gCamsAnimating) {
            float T = currentFrame;
            float angle1 = T * camSpeed;
            float angle2 = angle1 + pi<float>();
            float bobY   = sin(T * 0.5f) * 1.0f;

            if ((int)objects.size() > 4) {
                vec3 p1 = vec3(
                    camCenter.x + camRadius * cos(angle1),
                    camCenter.y + bobY,
                    camCenter.z + camRadius * sin(angle1));
                objects[4].position = p1;
                float dx1 = camCenter.x - p1.x;
                float dz1 = camCenter.z - p1.z;
                objects[4].rotation.y = degrees(atan2(dx1, dz1)) + 90.0f;
                gLights[4].position = p1;
            }
            if ((int)objects.size() > 5) {
                vec3 p2 = vec3(
                    camCenter.x + camRadius * cos(angle2),
                    camCenter.y - bobY,
                    camCenter.z + camRadius * sin(angle2));
                objects[5].position = p2;
                float dx2 = camCenter.x - p2.x;
                float dz2 = camCenter.z - p2.z;
                objects[5].rotation.y = degrees(atan2(dx2, dz2)) + 90.0f;
                gLights[5].position = p2;
            }
        }

        glClearColor(0.04f, 0.04f, 0.07f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // MATRIZ VIEW e PROJECTION — modo orbital ou FPS (tecla F alterna)
        mat4 view, proj;
        vec3 eye;
        if (gFPSMode) {
            eye  = gFPSCam.position;
            view = gFPSCam.getView();
            proj = perspective(radians(gFPSCam.fov), (float)WIDTH / HEIGHT, 0.1f, 200.0f);
        } else {
            eye.x = gCamRadius * cos(radians(gCamPitch)) * cos(radians(gCamYaw));
            eye.y = gCamRadius * sin(radians(gCamPitch));
            eye.z = gCamRadius * cos(radians(gCamPitch)) * sin(radians(gCamYaw));
            view  = lookAt(eye, vec3(0.0f, -4.0f, -2.0f), vec3(0.0f, 1.0f, 0.0f));
            proj  = perspective(radians(45.0f), (float)WIDTH / HEIGHT, 0.1f, 200.0f);
        }

        glUniformMatrix4fv(viewLoc, 1, GL_FALSE, value_ptr(view));
        glUniformMatrix4fv(projLoc, 1, GL_FALSE, value_ptr(proj));
        glUniform3fv(camLoc, 1, value_ptr(eye));

        sendLightUniforms(shaderID, gLightStatus);

        glUniform1f(kaLoc, 0.10f);
        glUniform1f(kdLoc, 0.75f);
        glUniform1f(ksLoc, 0.45f);
        glUniform1f(qLoc,  32.0f);

        // =========================================================
        //  Desenho de todos os objetos
        // =========================================================
        for (int i = 0; i < (int)objects.size(); i++)
        {
            auto& obj = objects[i];
            if (!obj.VAO) continue;

            // MATRIZ MODEL — translacao + rotacao + escala do objeto
            mat4 model = translate(mat4(1.0f), obj.position);
            model = rotate(model, radians(obj.rotation.x), vec3(1.0f, 0.0f, 0.0f));
            model = rotate(model, radians(obj.rotation.y), vec3(0.0f, 1.0f, 0.0f));
            model = rotate(model, radians(obj.rotation.z), vec3(0.0f, 0.0f, 1.0f));
            model = scale(model, vec3(obj.scale));
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, value_ptr(model));

            int useTex = (obj.texID != 0 && obj.useTexture) ? 1 : 0;
            glUniform1i(useTextureLoc, useTex);
            if (useTex)
                glBindTexture(GL_TEXTURE_2D, obj.texID);
            else
                glBindTexture(GL_TEXTURE_2D, 0);

            glBindVertexArray(obj.VAO);
            glDrawArrays(GL_TRIANGLES, 0, obj.nVertices);
            glBindVertexArray(0);
            glBindTexture(GL_TEXTURE_2D, 0);
        }

        glfwSwapBuffers(window);
    }

    for (auto& obj : objects) {
        if (obj.VAO)   glDeleteVertexArrays(1, &obj.VAO);
        if (obj.texID) glDeleteTextures(1, &obj.texID);
    }
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

    if (!gObjects || gObjects->empty()) return;
    auto& objects = *gObjects;

    if (action == GLFW_PRESS) {
        // Teclas 1-3: alternam cada lustre (Key / Fill / Back light)
        if (key == GLFW_KEY_1) {
            gLightStatus[0] = !gLightStatus[0];
            cout << gLights[0].name << ": " << (gLightStatus[0] ? "ON" : "OFF") << endl;
        }
        if (key == GLFW_KEY_2) {
            gLightStatus[1] = !gLightStatus[1];
            cout << gLights[1].name << ": " << (gLightStatus[1] ? "ON" : "OFF") << endl;
        }
        if (key == GLFW_KEY_3) {
            gLightStatus[2] = !gLightStatus[2];
            cout << gLights[2].name << ": " << (gLightStatus[2] ? "ON" : "OFF") << endl;
        }
        if (key == GLFW_KEY_4) {
            gLightStatus[4] = !gLightStatus[4];
            cout << gLights[4].name << ": " << (gLightStatus[4] ? "ON" : "OFF") << endl;
        }
        if (key == GLFW_KEY_5) {
            gLightStatus[5] = !gLightStatus[5];
            cout << gLights[5].name << ": " << (gLightStatus[5] ? "ON" : "OFF") << endl;
        }
        if (key == GLFW_KEY_6) {
            gLightStatus[6] = !gLightStatus[6];
            cout << gLights[6].name << ": " << (gLightStatus[6] ? "ON" : "OFF") << endl;
        }
        if (key == GLFW_KEY_7) {
            gLightStatus[7] = !gLightStatus[7];
            cout << gLights[7].name << ": " << (gLightStatus[7] ? "ON" : "OFF") << endl;
        }
        if (key == GLFW_KEY_8) {
            gLightStatus[8] = !gLightStatus[8];
            cout << gLights[8].name << ": " << (gLightStatus[8] ? "ON" : "OFF") << endl;
        }

        // T — toggle textura em todos os objetos simultaneamente
        if (key == GLFW_KEY_T) {
            static bool texOn = true;
            texOn = !texOn;
            for (auto& obj : objects) obj.useTexture = texOn;
            cout << "useTexture global: " << (texOn ? "1 (textura .mtl)" : "0 (cor fallback)") << endl;
        }

        // L — imprime estado atual de todos os guarda-chuvas
        if (key == GLFW_KEY_L) {
            cout << "\n--- Guarda-chuvas de iluminacao ---" << endl;
            for (int i = 0; i < 4; i++)
                cout << "  " << gLights[i].name << ": "
                     << (gLightStatus[i] ? "ON" : "OFF") << endl;
        }

        // SPACE — pause/resume animacao Bezier do microfone
        if (key == GLFW_KEY_SPACE) {
            gAnimating = !gAnimating;
            cout << "Microfone Bezier: " << (gAnimating ? "LIGADO" : "PAUSADO") << endl;
        }

        // P — pause/resume orbita das cameras
        if (key == GLFW_KEY_P) {
            gCamsAnimating = !gCamsAnimating;
            cout << "Cameras: " << (gCamsAnimating ? "ORBITANDO" : "PAUSADAS") << endl;
        }

        // F — alterna entre camera orbital e camera FPS
        if (key == GLFW_KEY_F) {
            gFPSMode = !gFPSMode;
            gFirstMouse = true;
            if (gFPSMode) {
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                cout << "Camera: FPS (W/A/S/D/Q/E + mouse)" << endl;
            } else {
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                cout << "Camera: Orbital (mouse drag + scroll)" << endl;
            }
        }
    }
}

// =========================================================
//  mouse_button_callback
// =========================================================
void mouse_button_callback(GLFWwindow* window, int button, int action, int mods)
{
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        gMouseDown = (action == GLFW_PRESS);
        if (gMouseDown) {
            double x, y;
            glfwGetCursorPos(window, &x, &y);
            gLastX = (float)x;
            gLastY = (float)y;
        }
    }
}

// =========================================================
//  mouse_callback — rotaciona camera orbital
// =========================================================
void mouse_callback(GLFWwindow* window, double xpos, double ypos)
{
    if (gFPSMode) {
        if (gFirstMouse) { gLastX = (float)xpos; gLastY = (float)ypos; gFirstMouse = false; }
        float dx =  (float)xpos - gLastX;
        float dy =  gLastY - (float)ypos;
        gLastX = (float)xpos;
        gLastY = (float)ypos;
        gFPSCam.rotacionar(dx, dy);
        return;
    }
    if (!gMouseDown) return;
    float dx = (float)xpos - gLastX;
    float dy = (float)ypos - gLastY;
    gLastX = (float)xpos;
    gLastY = (float)ypos;
    gCamYaw   += dx * 0.3f;
    gCamPitch += dy * 0.3f;
    if (gCamPitch >  89.0f) gCamPitch =  89.0f;
    if (gCamPitch < -89.0f) gCamPitch = -89.0f;
}

// =========================================================
//  scroll_callback — zoom
// =========================================================
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset)
{
    if (gFPSMode) { gFPSCam.zoom((float)yoffset); return; }
    gCamRadius -= (float)yoffset * 0.5f;
    if (gCamRadius <  2.0f) gCamRadius =  2.0f;
    if (gCamRadius > 50.0f) gCamRadius = 50.0f;
}

// =========================================================
//  Envia as 3 luzes Phong ao fragment shader.
//  lightStatus[0..3]: estado ON/OFF de cada guarda-chuva de studio.
//  Quando um guarda-chuva esta OFF, sua contribuicao e zerada
//  multiplicando a cor da luz por 0 antes de enviar ao shader.
// =========================================================
void sendLightUniforms(GLuint shader, const bool lightStatus[9])
{
    char buf[64];
    for (int i = 0; i < 9; i++) {
        auto ul = [&](const char* member) -> GLint {
            snprintf(buf, sizeof(buf), "lights[%d].%s", i, member);
            return glGetUniformLocation(shader, buf);
        };
        // Se o guarda-chuva correspondente estiver OFF, envia cor zerada
        vec3 cor = lightStatus[i] ? gLights[i].color : vec3(0.0f);

        glUniform3fv(ul("position"),  1, value_ptr(gLights[i].position));
        glUniform3fv(ul("color"),     1, value_ptr(cor));
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
//  loadMTL — extrai nome da textura difusa do arquivo .mtl
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
//  loadOBJWithNormals  <-- PARSER OBJ
//
//  Le um arquivo .obj e constroi um VAO com buffer interleaved:
//    posicao(3) + cor(3) + normal(3) + uv(2) = 11 floats/vertice
//
//  Suporte: v, vt, vn, faces f (v/t/n), fan triangulation,
//           .mtl referenciado -> carrega textura automaticamente.
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
    if (!file.is_open()) {
        cerr << "Erro ao abrir OBJ: " << filePath << endl;
        nVertices = 0;
        return 0;
    }

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
                FV fface{0, -1, -1};
                istringstream face(ww); string idx;
                if (getline(face, idx, '/')) fface.vi = idx.empty() ? 0 : stoi(idx)-1;
                if (getline(face, idx, '/')) fface.ti = idx.empty() ? -1 : stoi(idx)-1;
                if (getline(face, idx))      fface.ni = idx.empty() ? -1 : stoi(idx)-1;
                fv.push_back(fface);
            }
            // Fan triangulation: converte poligono em triangulos
            for (int k = 1; k+1 < (int)fv.size(); k++) {
                for (int t : {0, k, k+1}) {
                    int vi = fv[t].vi, ti = fv[t].ti, ni = fv[t].ni;
                    auto& p = positions[vi];
                    vBuffer.insert(vBuffer.end(), {p.x, p.y, p.z,
                        fallbackColor.r, fallbackColor.g, fallbackColor.b});
                    if (ni >= 0 && ni < (int)normals.size()) {
                        auto& n = normals[ni];
                        vBuffer.insert(vBuffer.end(), {n.x, n.y, n.z});
                    } else {
                        vBuffer.insert(vBuffer.end(), {0.f, 1.f, 0.f});
                    }
                    if (ti >= 0 && ti < (int)texCoords.size()) {
                        auto& tc = texCoords[ti];
                        vBuffer.insert(vBuffer.end(), {tc.s, tc.t});
                    } else {
                        vBuffer.insert(vBuffer.end(), {0.f, 0.f});
                    }
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
    cout << "OBJ: " << filePath << "  ("
         << nVertices << " verts" << (texID ? ", textura" : "") << ")" << endl;

    GLuint VBO, VAO;
    glGenBuffers(1, &VBO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, vBuffer.size()*sizeof(GLfloat),
                 vBuffer.data(), GL_STATIC_DRAW);

    glGenVertexArrays(1, &VAO);
    glBindVertexArray(VAO);

    const GLsizei S = 11 * sizeof(GLfloat);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, S, (GLvoid*)0);                    // posicao
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, S, (GLvoid*)(3*sizeof(GLfloat)));  // cor
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, S, (GLvoid*)(6*sizeof(GLfloat)));  // normal
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, S, (GLvoid*)(9*sizeof(GLfloat)));  // uv
    glEnableVertexAttribArray(3);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    return VAO;
}

// =========================================================
//  printControls
// =========================================================
// =========================================================
//  generateQuad — cria um retangulo plano com UV para exibir textura
//  Buffer: pos(3) + cor(3) + normal(3) + uv(2) = 11 floats/vertice
// =========================================================
GLuint generateQuad(float w, float h, int& nVertices)
{
    float hw = w * 0.5f;
    float hh = h * 0.5f;

    // 2 triangulos formando o retangulo, normal apontando para +Z
    vector<GLfloat> v = {
    //  x     y     z     r     g     b     nx    ny    nz    u     v
       -hw,  -hh,  0.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
        hw,  -hh,  0.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f,
        hw,   hh,  0.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f,
       -hw,  -hh,  0.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
        hw,   hh,  0.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f,
       -hw,   hh,  0.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f,
    };

    nVertices = 6;
    GLuint VBO, VAO;
    glGenBuffers(1, &VBO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, v.size()*sizeof(GLfloat), v.data(), GL_STATIC_DRAW);
    glGenVertexArrays(1, &VAO);
    glBindVertexArray(VAO);
    const GLsizei S = 11 * sizeof(GLfloat);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, S, (GLvoid*)0);                    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, S, (GLvoid*)(3*sizeof(GLfloat)));  glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, S, (GLvoid*)(6*sizeof(GLfloat)));  glEnableVertexAttribArray(2);
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, S, (GLvoid*)(9*sizeof(GLfloat)));  glEnableVertexAttribArray(3);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    return VAO;
}

void printControls(const vector<OBJModel>& objects)
{
    cout << "\n========== ProvaGB - Demonstracao Tecnica ==========\n";
    cout << "Luzes (teclas 1-8):\n";
    cout << "  1 : Key Light   (lustre central)\n";
    cout << "  2 : Fill Light  (lustre esquerda)\n";
    cout << "  3 : Back Light  (lustre direita)\n";
    cout << "  4 : Spotlight camera_1\n";
    cout << "  5 : Spotlight camera_2\n";
    cout << "  6 : Face top    7 : Face left    8 : Face right\n";
    cout << "\nAnimacoes:\n";
    cout << "  SPACE : pause/resume microfone (Bezier)\n";
    cout << "  P     : pause/resume orbita das cameras\n";
    cout << "\nCamera:\n";
    cout << "  F              : alterna Orbital <-> FPS\n";
    cout << "  Orbital        : mouse drag + scroll\n";
    cout << "  FPS            : W/A/S/D/Q/E + mouse\n";
    cout << "\nObjetos:\n";
    cout << "  Z / X          : rotacionar RuPaul\n";
    cout << "  Setas / Y / U  : mover batom (X/Z/Y)\n";
    cout << "  C / V          : rotacionar batom\n";
    cout << "  B / N          : escala batom\n";
    cout << "  T              : toggle textura (0=cor / 1=.mtl)\n";
    cout << "  L              : listar estado das luzes\n";
    cout << "  ESC            : sair\n";
    cout << "====================================================\n\n";
}
