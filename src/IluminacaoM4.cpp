/* IluminacaoM4 - Modelo de Iluminação de Phong com leitura de OBJ/MTL
 *
 * Módulo 4 - Computação Gráfica - Unisinos
 * Autor: Kevin de Azevedo Garcia
 *
 * Demonstra a implementação completa do modelo de Phong a partir de
 * dados lidos dos arquivos .OBJ e .MTL:
 *
 *  1. OBJ  → vértices (v), normais (vn), coordenadas de textura (vt) e faces (f)
 *  2. MTL  → Ka (ambiente), Kd (difuso), Ks (especular), Ns (brilho), map_Kd (textura)
 *
 * Fórmula de Phong implementada no Fragment Shader:
 *   I = Ka·La·Cobj + Kd·max(N·L,0)·Ld·Cobj + Ks·max(R·V,0)^Ns·Ls
 *
 * onde:
 *   Ka, Kd, Ks  = coeficientes lidos do MTL (escalares — média dos canais RGB)
 *   Ns          = expoente especular lido do MTL
 *   La, Ld, Ls  = intensidade da luz (ambiente, difusa, especular)
 *   Cobj        = cor do objeto (textura ou cor de fallback)
 *   N           = normal do fragmento (espaço mundo)
 *   L           = vetor para a luz
 *   R           = reflexo de L em torno de N
 *   V           = vetor para a câmera
 *
 * Cena: Suzanne no centro com câmera FPS. A luz é uma esfera branca que
 * orbita automaticamente o objeto (pode ser pausada com P).
 *
 * Controles:
 *   W/A/S/D / Espaço / LShift  : mover câmera (FPS)
 *   Mouse                      : olhar ao redor
 *   M                          : capturar / liberar cursor
 *   P                          : pausar / retomar órbita da luz
 *   1                          : mostrar apenas parcela Ambiente
 *   2                          : mostrar apenas parcela Difusa
 *   3                          : mostrar apenas parcela Especular
 *   4                          : mostrar Phong completo (padrão)
 *   ESC                        : sair
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
//  Buffer por vértice — 11 floats:
//    location 0 : posição  xyz     (offset  0)
//    location 1 : cor      rgb     (offset  3)  ← fallback sem textura
//    location 2 : normal   xyz     (offset  6)  ← lida do OBJ (vn)
//    location 3 : texcoord st      (offset  9)  ← lida do OBJ (vt)
//
//  A normal é multiplicada pelo inverso-transposto da matriz modelo
//  para permanecer correta sob rotação e escala uniforme.
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

out vec3 fragPos;       // posição do fragmento no espaço mundo
out vec3 vNormal;       // normal no espaço mundo
out vec3 vColor;        // cor de fallback
out vec2 fragTexCoord;  // coordenada de textura

void main()
{
    vec4 worldPos  = model * vec4(position, 1.0);
    gl_Position    = projection * view * worldPos;

    fragPos        = vec3(worldPos);

    // Transformação correta da normal para o espaço mundo
    // mat3(transpose(inverse(model))) corrige normais sob escala não-uniforme
    vNormal        = mat3(transpose(inverse(model))) * normal;

    vColor         = color;
    fragTexCoord   = texCoord;
}
)";

// =========================================================
//  Fragment Shader — Modelo de Phong
//
//  Uniforms de material (lidos do .MTL pela aplicação):
//    ka  = coeficiente ambiente   (Ka do MTL)
//    kd  = coeficiente difuso     (Kd do MTL)
//    ks  = coeficiente especular  (Ks do MTL)
//    q   = expoente especular     (Ns do MTL)
//
//  phongMode controla o que é exibido (para fins didáticos):
//    0 = apenas ambiente
//    1 = apenas difuso
//    2 = apenas especular
//    3 = Phong completo (padrão)
// =========================================================
const GLchar* fragmentShaderSource = R"(
#version 450

in vec3 fragPos;
in vec3 vNormal;
in vec3 vColor;
in vec2 fragTexCoord;

out vec4 fragColor;

// ---- Material (coeficientes lidos do MTL) ----
uniform float ka;   // coeficiente de reflexão ambiente
uniform float kd;   // coeficiente de reflexão difusa
uniform float ks;   // coeficiente de reflexão especular
uniform float q;    // expoente especular (brilho)

// ---- Textura ----
uniform sampler2D texBuff;
uniform int       useTexture;   // 1 = amostrar texBuff, 0 = usar vColor

// ---- Luz pontual ----
uniform vec3  lightPos;     // posição da luz no espaço mundo
uniform vec3  lightColor;   // cor/intensidade da luz

// ---- Câmera ----
uniform vec3 camPos;        // posição da câmera (para vetor de visão V)

// ---- Modo didático ----
uniform int phongMode;  // 0=ambiente, 1=difuso, 2=especular, 3=completo

void main()
{
    // ---- Cor base do objeto ----------------------------------------
    // Vem da textura (map_Kd do MTL) ou da cor de fallback do vértice
    vec3 objColor = (useTexture == 1)
                    ? vec3(texture(texBuff, fragTexCoord))
                    : vColor;

    // ---- Vetores necessários para Phong ----------------------------
    vec3 N = normalize(vNormal);              // normal do fragmento
    vec3 L = normalize(lightPos - fragPos);   // direção para a luz
    vec3 V = normalize(camPos   - fragPos);   // direção para a câmera
    vec3 R = normalize(reflect(-L, N));       // reflexão de L em N

    // ================================================================
    //  PARCELA AMBIENTE
    //  Simula a luz indireta que ilumina uniformemente todas as faces.
    //  I_amb = ka * La * Cobj
    // ================================================================
    vec3 ambient = ka * lightColor * objColor;

    // ================================================================
    //  PARCELA DIFUSA (Lambert)
    //  Depende do ângulo entre a normal e o vetor de luz.
    //  max(N·L, 0) garante que faces voltadas para longe da luz
    //  não recebam contribuição negativa.
    //  I_dif = kd * max(N·L, 0) * Ld * Cobj
    // ================================================================
    float diff    = max(dot(N, L), 0.0);
    vec3  diffuse = kd * diff * lightColor * objColor;

    // ================================================================
    //  PARCELA ESPECULAR (Phong)
    //  Depende do ângulo entre o reflexo da luz (R) e o vetor de visão.
    //  O expoente q (Ns no MTL) controla a concentração do highlight:
    //  valores altos → realce pequeno e brilhante.
    //  I_esp = ks * max(R·V, 0)^q * Ls
    //  (Nota: especular geralmente não multiplica pela cor do objeto)
    // ================================================================
    float spec    = pow(max(dot(R, V), 0.0), q);
    vec3  specular = ks * spec * lightColor;

    // ================================================================
    //  Composição final conforme modo selecionado
    // ================================================================
    vec3 result;
    if      (phongMode == 0) result = ambient;
    else if (phongMode == 1) result = diffuse;
    else if (phongMode == 2) result = specular;
    else                     result = ambient + diffuse + specular;

    fragColor = vec4(result, 1.0);
}
)";

// =========================================================
//  Struct Material — coeficientes lidos do MTL
// =========================================================
struct Material
{
    float  ka    = 0.1f;   // coeficiente ambiente  (Ka do MTL)
    float  kd    = 0.8f;   // coeficiente difuso    (Kd do MTL)
    float  ks    = 0.5f;   // coeficiente especular (Ks do MTL)
    float  q     = 32.0f;  // expoente especular    (Ns do MTL)
    GLuint texID = 0;       // textura difusa (map_Kd)
};

// =========================================================
//  Struct do modelo OBJ na cena
// =========================================================
struct OBJModel
{
    GLuint   VAO       = 0;
    int      nVertices = 0;
    Material material;
    vec3     position  = vec3(0.0f);
    float    scale     = 1.0f;
    vec3     fallbackColor = vec3(0.8f, 0.7f, 0.6f); // usado se sem textura
};

// =========================================================
//  Classe Camera (FPS — igual ao DesafioM5)
// =========================================================
enum CameraDir { CAM_FORWARD, CAM_BACKWARD, CAM_LEFT, CAM_RIGHT, CAM_UP, CAM_DOWN };

class Camera
{
public:
    vec3  position, front, up, right, worldUp;
    float yaw, pitch, speed, sensitivity, fov;

    Camera(vec3 pos = vec3(0,0,4), vec3 wUp = vec3(0,1,0),
           float yaw = -90.f, float pitch = 0.f)
        : position(pos), worldUp(wUp), yaw(yaw), pitch(pitch),
          speed(3.0f), sensitivity(0.1f), fov(45.f)
    { update(); }

    mat4 getViewMatrix() const { return lookAt(position, position+front, up); }

    void mover(CameraDir d, float dt)
    {
        float v = speed * dt;
        if (d == CAM_FORWARD)  position += front   * v;
        if (d == CAM_BACKWARD) position -= front   * v;
        if (d == CAM_LEFT)     position -= right   * v;
        if (d == CAM_RIGHT)    position += right   * v;
        if (d == CAM_UP)       position += worldUp * v;
        if (d == CAM_DOWN)     position -= worldUp * v;
    }

    void rotacionar(float xoff, float yoff)
    {
        yaw   += xoff * sensitivity;
        pitch += yoff * sensitivity;
        pitch = std::max(-89.f, std::min(89.f, pitch));
        update();
    }

    void zoom(float y) { fov = std::max(5.f, std::min(90.f, fov - y)); }

private:
    void update()
    {
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
const GLuint WIDTH = 1200, HEIGHT = 800;

Camera gCamera;
bool   mouseCaptured = true;
bool   firstMouse    = true;
float  lastX = WIDTH/2.f, lastY = HEIGHT/2.f;

int  gPhongMode  = 3;    // 3 = Phong completo
bool gLightOrbit = true; // luz orbita o objeto

// =========================================================
//  Protótipos
// =========================================================
void   key_callback(GLFWwindow*, int, int, int, int);
void   mouse_callback(GLFWwindow*, double, double);
void   scroll_callback(GLFWwindow*, double, double);
int    setupShader();
GLuint loadTexture(const string&);
Material loadMTL(const string& mtlPath, const string& dir);
GLuint loadOBJWithNormals(const string& filePath, int& nVertices,
                           Material& mat, vec3 fallbackColor);
GLuint generateSphereSimple(float r, int latSeg, int lonSeg, int& nV, vec3 col);

// =========================================================
//  main
// =========================================================
int main()
{
    glfwInit();
    GLFWwindow* window = glfwCreateWindow(WIDTH, HEIGHT,
        "IluminacaoM4 - Phong OBJ/MTL -- Kevin Garcia", nullptr, nullptr);
    glfwMakeContextCurrent(window);
    glfwSetKeyCallback(window, key_callback);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetScrollCallback(window, scroll_callback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);
    cout << "Renderer : " << glGetString(GL_RENDERER) << "\n"
         << "OpenGL   : " << glGetString(GL_VERSION)  << "\n";

    int fw, fh;
    glfwGetFramebufferSize(window, &fw, &fh);
    glViewport(0, 0, fw, fh);

    GLuint shaderID = setupShader();
    glUseProgram(shaderID);

    // --- Carregar Suzanne ---
    OBJModel suzanne;
    suzanne.fallbackColor = vec3(0.85f, 0.72f, 0.60f);
    suzanne.position      = vec3(0.0f);
    suzanne.scale         = 1.0f;
    suzanne.VAO = loadOBJWithNormals("../assets/Modelos3D/Suzanne.obj",
                                      suzanne.nVertices,
                                      suzanne.material,
                                      suzanne.fallbackColor);
    if (!suzanne.VAO) { cerr << "Falha ao carregar OBJ.\n"; glfwTerminate(); return -1; }

    cout << "\n--- Material lido do MTL ---\n"
         << "  ka = " << suzanne.material.ka << "\n"
         << "  kd = " << suzanne.material.kd << "\n"
         << "  ks = " << suzanne.material.ks << "\n"
         << "  q  = " << suzanne.material.q  << "\n"
         << "  textura: " << (suzanne.material.texID ? "sim" : "nao") << "\n";

    // --- Esfera marcadora da luz ---
    int    nSphV;
    GLuint sphereVAO = generateSphereSimple(0.08f, 12, 12, nSphV, vec3(1,1,0.5f));

    // --- Câmera ---
    gCamera = Camera(vec3(0, 1, 4));

    // --- Uniforms estáticos ---
    glUniform1i(glGetUniformLocation(shaderID, "texBuff"), 0);
    glActiveTexture(GL_TEXTURE0);

    // Cor da luz (branca)
    vec3 lightColor = vec3(1.0f, 1.0f, 1.0f);
    glUniform3fv(glGetUniformLocation(shaderID, "lightColor"), 1, value_ptr(lightColor));

    glEnable(GL_DEPTH_TEST);

    cout << "\n=== Controles ===\n"
         << "W/A/S/D  Espaco/LShift : mover camera\n"
         << "Mouse                  : olhar ao redor\n"
         << "M                      : capturar/liberar cursor\n"
         << "P                      : pausar/retomar orbita da luz\n"
         << "1  : somente Ambiente\n"
         << "2  : somente Difuso\n"
         << "3  : somente Especular\n"
         << "4  : Phong Completo (padrao)\n"
         << "ESC: sair\n";

    float lastFrame = 0.f;

    // =========================================================
    //  Loop principal
    // =========================================================
    while (!glfwWindowShouldClose(window))
    {
        float now       = (float)glfwGetTime();
        float deltaTime = now - lastFrame;
        lastFrame       = now;

        glfwPollEvents();

        // Movimento contínuo da câmera
        if (glfwGetKey(window, GLFW_KEY_W)            == GLFW_PRESS) gCamera.mover(CAM_FORWARD,  deltaTime);
        if (glfwGetKey(window, GLFW_KEY_S)            == GLFW_PRESS) gCamera.mover(CAM_BACKWARD, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_A)            == GLFW_PRESS) gCamera.mover(CAM_LEFT,     deltaTime);
        if (glfwGetKey(window, GLFW_KEY_D)            == GLFW_PRESS) gCamera.mover(CAM_RIGHT,    deltaTime);
        if (glfwGetKey(window, GLFW_KEY_SPACE)        == GLFW_PRESS) gCamera.mover(CAM_UP,       deltaTime);
        if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT)   == GLFW_PRESS) gCamera.mover(CAM_DOWN,     deltaTime);

        glClearColor(0.05f, 0.05f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // --- View / Projection (atualizado por frame pois câmera se move) ---
        mat4 view = gCamera.getViewMatrix();
        mat4 proj = perspective(radians(gCamera.fov), (float)WIDTH/HEIGHT, 0.1f, 100.f);
        glUniformMatrix4fv(glGetUniformLocation(shaderID,"view"),       1, GL_FALSE, value_ptr(view));
        glUniformMatrix4fv(glGetUniformLocation(shaderID,"projection"), 1, GL_FALSE, value_ptr(proj));
        glUniform3fv(glGetUniformLocation(shaderID,"camPos"), 1, value_ptr(gCamera.position));

        // --- Posição da luz (órbita ao redor do objeto) ---
        float orbitAngle = gLightOrbit ? now * 0.8f : 1.0f;
        vec3 lightPos = vec3(cos(orbitAngle) * 3.0f, 2.0f, sin(orbitAngle) * 3.0f);
        glUniform3fv(glGetUniformLocation(shaderID,"lightPos"), 1, value_ptr(lightPos));

        // --- Modo Phong didático ---
        glUniform1i(glGetUniformLocation(shaderID,"phongMode"), gPhongMode);

        // =============================================================
        //  Desenhar Suzanne com Phong completo
        // =============================================================
        {
            mat4 model = translate(mat4(1.f), suzanne.position);
            model = scale(model, vec3(suzanne.scale));
            glUniformMatrix4fv(glGetUniformLocation(shaderID,"model"), 1, GL_FALSE, value_ptr(model));

            // Enviar coeficientes lidos do MTL
            glUniform1f(glGetUniformLocation(shaderID,"ka"), suzanne.material.ka);
            glUniform1f(glGetUniformLocation(shaderID,"kd"), suzanne.material.kd);
            glUniform1f(glGetUniformLocation(shaderID,"ks"), suzanne.material.ks);
            glUniform1f(glGetUniformLocation(shaderID,"q"),  suzanne.material.q);

            if (suzanne.material.texID) {
                glUniform1i(glGetUniformLocation(shaderID,"useTexture"), 1);
                glBindTexture(GL_TEXTURE_2D, suzanne.material.texID);
            } else {
                glUniform1i(glGetUniformLocation(shaderID,"useTexture"), 0);
                glBindTexture(GL_TEXTURE_2D, 0);
            }

            glBindVertexArray(suzanne.VAO);
            glDrawArrays(GL_TRIANGLES, 0, suzanne.nVertices);
            glBindVertexArray(0);
            glBindTexture(GL_TEXTURE_2D, 0);
        }

        // =============================================================
        //  Marcador da luz (esfera emissiva: ka=1, kd=ks=0)
        //  Independe do phongMode — sempre visível como referência
        // =============================================================
        {
            glUniform1i(glGetUniformLocation(shaderID,"phongMode"),   3); // completo
            glUniform1i(glGetUniformLocation(shaderID,"useTexture"),  0);
            glUniform1f(glGetUniformLocation(shaderID,"ka"),  1.0f);
            glUniform1f(glGetUniformLocation(shaderID,"kd"),  0.0f);
            glUniform1f(glGetUniformLocation(shaderID,"ks"),  0.0f);

            mat4 model = translate(mat4(1.f), lightPos);
            glUniformMatrix4fv(glGetUniformLocation(shaderID,"model"), 1, GL_FALSE, value_ptr(model));
            glBindVertexArray(sphereVAO);
            glDrawArrays(GL_TRIANGLES, 0, nSphV);
            glBindVertexArray(0);
        }

        glfwSwapBuffers(window);
    }

    glDeleteVertexArrays(1, &suzanne.VAO);
    glDeleteVertexArrays(1, &sphereVAO);
    if (suzanne.material.texID) glDeleteTextures(1, &suzanne.material.texID);
    glfwTerminate();
    return 0;
}

// =========================================================
//  Callbacks
// =========================================================
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mode)
{
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
        glfwSetWindowShouldClose(window, GL_TRUE);

    if (key == GLFW_KEY_M && action == GLFW_PRESS) {
        mouseCaptured = !mouseCaptured;
        firstMouse    = true;
        glfwSetInputMode(window, GLFW_CURSOR,
            mouseCaptured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    }
    if (key == GLFW_KEY_P && action == GLFW_PRESS) {
        gLightOrbit = !gLightOrbit;
        cout << "Orbita da luz: " << (gLightOrbit ? "ligada" : "pausada") << "\n";
    }

    // Modo Phong didático
    if (key == GLFW_KEY_1 && action == GLFW_PRESS) { gPhongMode = 0; cout << "Modo: Ambiente\n"; }
    if (key == GLFW_KEY_2 && action == GLFW_PRESS) { gPhongMode = 1; cout << "Modo: Difuso\n"; }
    if (key == GLFW_KEY_3 && action == GLFW_PRESS) { gPhongMode = 2; cout << "Modo: Especular\n"; }
    if (key == GLFW_KEY_4 && action == GLFW_PRESS) { gPhongMode = 3; cout << "Modo: Phong Completo\n"; }
}

void mouse_callback(GLFWwindow*, double xpos, double ypos)
{
    if (!mouseCaptured) return;
    if (firstMouse) { lastX = (float)xpos; lastY = (float)ypos; firstMouse = false; }
    float xoff =  (float)xpos - lastX;
    float yoff =  lastY - (float)ypos;
    lastX = (float)xpos; lastY = (float)ypos;
    gCamera.rotacionar(xoff, yoff);
}

void scroll_callback(GLFWwindow*, double, double yoff) { gCamera.zoom((float)yoff); }

// =========================================================
//  setupShader
// =========================================================
int setupShader()
{
    GLint ok; GLchar log[512];
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &vertexShaderSource, NULL); glCompileShader(vs);
    glGetShaderiv(vs, GL_COMPILE_STATUS, &ok);
    if (!ok) { glGetShaderInfoLog(vs,512,NULL,log); cerr<<"VS:\n"<<log; }

    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &fragmentShaderSource, NULL); glCompileShader(fs);
    glGetShaderiv(fs, GL_COMPILE_STATUS, &ok);
    if (!ok) { glGetShaderInfoLog(fs,512,NULL,log); cerr<<"FS:\n"<<log; }

    GLuint p = glCreateProgram();
    glAttachShader(p,vs); glAttachShader(p,fs); glLinkProgram(p);
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { glGetProgramInfoLog(p,512,NULL,log); cerr<<"LINK:\n"<<log; }
    glDeleteShader(vs); glDeleteShader(fs);
    return p;
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
    int w, h, ch;
    stbi_set_flip_vertically_on_load(true);
    unsigned char* data = stbi_load(filePath.c_str(), &w, &h, &ch, 0);
    if (data) {
        GLenum fmt = (ch==4) ? GL_RGBA : GL_RGB;
        glTexImage2D(GL_TEXTURE_2D,0,fmt,w,h,0,fmt,GL_UNSIGNED_BYTE,data);
        glGenerateMipmap(GL_TEXTURE_2D);
        cout << "Textura: " << filePath << " (" << w << "x" << h << ")\n";
    } else {
        cerr << "Falha na textura: " << filePath << "\n";
        glDeleteTextures(1,&texID); texID=0;
    }
    stbi_image_free(data);
    glBindTexture(GL_TEXTURE_2D, 0);
    return texID;
}

// =========================================================
//  loadMTL — lê Ka, Kd, Ks, Ns e map_Kd do arquivo .mtl
//
//  Conversão para escalares:
//    ka = média(Ka.rgb)   → coeficiente ambiente único
//    kd = média(Kd.rgb)   → coeficiente difuso único
//    ks = média(Ks.rgb)   → coeficiente especular único
//    q  = Ns              → expoente especular
//
//  Por que média? O modelo de Phong clássico usa escalares;
//  manter a cor do objeto separada (textura ou vColor) é mais
//  limpo do que misturá-la com os coeficientes RGB do MTL.
// =========================================================
Material loadMTL(const string& mtlPath, const string& dir)
{
    Material mat;
    ifstream file(mtlPath);
    if (!file.is_open()) {
        cerr << "MTL nao encontrado: " << mtlPath << "\n";
        return mat;
    }
    string line;
    while (getline(file, line)) {
        istringstream ss(line);
        string kw; ss >> kw;

        if (kw == "Ka") {
            vec3 v; ss >> v.r >> v.g >> v.b;
            mat.ka = (v.r + v.g + v.b) / 3.0f;
            // Clamp: Ka=1,1,1 do Blender seria ka=1.0 (muito alto).
            // Usamos um teto de 0.3 para que a cena não fique saturada.
            mat.ka = std::min(mat.ka, 0.3f);
        }
        else if (kw == "Kd") {
            vec3 v; ss >> v.r >> v.g >> v.b;
            mat.kd = (v.r + v.g + v.b) / 3.0f;
        }
        else if (kw == "Ks") {
            vec3 v; ss >> v.r >> v.g >> v.b;
            mat.ks = (v.r + v.g + v.b) / 3.0f;
        }
        else if (kw == "Ns") {
            ss >> mat.q;
        }
        else if (kw == "map_Kd") {
            string texName; ss >> texName;
            mat.texID = loadTexture(dir + texName);
        }
    }
    return mat;
}

// =========================================================
//  loadOBJWithNormals
//
//  Extensão do LoadSimpleOBJ do código base para incluir:
//    • Vetores normais (vn) — lidos e expandidos por face
//    • Coordenadas de textura (vt)
//    • Leitura automática do MTL referenciado (mtllib)
//
//  Buffer de saída: 11 floats por vértice
//    x y z | r g b | nx ny nz | s t
//    loc 0   loc 1   loc 2      loc 3
//
//  Fan triangulation: suporta faces com 3 ou mais vértices.
// =========================================================
GLuint loadOBJWithNormals(const string& filePath, int& nVertices,
                           Material& mat, vec3 fallbackColor)
{
    vector<vec3>    positions, normals;
    vector<vec2>    texCoords;
    vector<GLfloat> vBuffer;

    // Diretório base para resolver MTL e textura
    string dir;
    size_t sl = filePath.find_last_of("/\\");
    if (sl != string::npos) dir = filePath.substr(0, sl+1);

    string mtlFile;

    ifstream file(filePath);
    if (!file.is_open()) { cerr << "Erro ao abrir OBJ: " << filePath << "\n"; return 0; }

    string line;
    while (getline(file, line))
    {
        istringstream ss(line);
        string w; ss >> w;

        if (w == "mtllib") {
            // Nome do arquivo MTL vinculado ao OBJ
            ss >> mtlFile;
        }
        else if (w == "v") {
            // Posição do vértice: v x y z
            vec3 v; ss >> v.x >> v.y >> v.z;
            positions.push_back(v);
        }
        else if (w == "vt") {
            // Coordenada de textura: vt s t
            vec2 t; ss >> t.s >> t.t;
            texCoords.push_back(t);
        }
        else if (w == "vn") {
            // Vetor normal do vértice: vn nx ny nz
            // Cada normal corresponde a uma face, não a um vértice único;
            // o índice do vn é fornecido na linha "f".
            vec3 n; ss >> n.x >> n.y >> n.z;
            normals.push_back(n);
        }
        else if (w == "f")
        {
            // Face: f v1/vt1/vn1  v2/vt2/vn2  v3/vt3/vn3 [v4/...]
            // Índices são 1-based no formato OBJ.
            struct FV { int vi, ti, ni; };
            vector<FV> fv;

            while (ss >> w) {
                FV f{0,-1,-1};
                istringstream face(w); string idx;
                // Ler vi
                if (getline(face, idx, '/')) f.vi = idx.empty() ? 0 : stoi(idx)-1;
                // Ler ti (pode estar vazio em formato vi//ni)
                if (getline(face, idx, '/')) f.ti = idx.empty() ? -1 : stoi(idx)-1;
                // Ler ni
                if (getline(face, idx))      f.ni = idx.empty() ? -1 : stoi(idx)-1;
                fv.push_back(f);
            }

            // Fan triangulation: (v0,v1,v2), (v0,v2,v3), ...
            for (int k = 1; k+1 < (int)fv.size(); k++) {
                for (int t : {0, k, k+1}) {
                    int vi = fv[t].vi, ti = fv[t].ti, ni = fv[t].ni;

                    // Posição
                    vBuffer.push_back(positions[vi].x);
                    vBuffer.push_back(positions[vi].y);
                    vBuffer.push_back(positions[vi].z);
                    // Cor (fallback — sobrescrita pela textura no shader)
                    vBuffer.push_back(fallbackColor.r);
                    vBuffer.push_back(fallbackColor.g);
                    vBuffer.push_back(fallbackColor.b);
                    // Normal (vn do OBJ)
                    if (ni >= 0 && ni < (int)normals.size()) {
                        vBuffer.push_back(normals[ni].x);
                        vBuffer.push_back(normals[ni].y);
                        vBuffer.push_back(normals[ni].z);
                    } else {
                        // Fallback: normal apontando para cima
                        vBuffer.push_back(0.f); vBuffer.push_back(1.f); vBuffer.push_back(0.f);
                    }
                    // Coordenada de textura (vt do OBJ)
                    if (ti >= 0 && ti < (int)texCoords.size()) {
                        vBuffer.push_back(texCoords[ti].s);
                        vBuffer.push_back(texCoords[ti].t);
                    } else {
                        vBuffer.push_back(0.f); vBuffer.push_back(0.f);
                    }
                }
            }
        }
    }
    file.close();

    // Carregar material do MTL (Ka, Kd, Ks, Ns, map_Kd)
    if (!mtlFile.empty())
        mat = loadMTL(dir + mtlFile, dir);

    nVertices = (int)vBuffer.size() / 11;
    cout << "OBJ carregado: " << filePath
         << "  (" << nVertices << " vertices)\n";

    // --- Criar VAO e VBO ---
    GLuint VBO, VAO;
    glGenBuffers(1, &VBO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, vBuffer.size()*sizeof(GLfloat), vBuffer.data(), GL_STATIC_DRAW);

    glGenVertexArrays(1, &VAO);
    glBindVertexArray(VAO);

    const GLsizei stride = 11 * sizeof(GLfloat);

    // location 0: posição (3 floats, offset 0)
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (GLvoid*)0);
    glEnableVertexAttribArray(0);

    // location 1: cor fallback (3 floats, offset 3)
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (GLvoid*)(3*sizeof(GLfloat)));
    glEnableVertexAttribArray(1);

    // location 2: normal do OBJ - vn (3 floats, offset 6)
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, stride, (GLvoid*)(6*sizeof(GLfloat)));
    glEnableVertexAttribArray(2);

    // location 3: coordenada de textura - vt (2 floats, offset 9)
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, stride, (GLvoid*)(9*sizeof(GLfloat)));
    glEnableVertexAttribArray(3);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    return VAO;
}

// =========================================================
//  generateSphereSimple — marcador de luz
//  Mesmo layout de 11 floats do OBJ loader
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
        return {radius*cos(phi)*sin(theta), radius*cos(theta), radius*sin(phi)*sin(theta)};
    };
    for (int i=0;i<latSeg;i++) for (int j=0;j<lonSeg;j++) {
        vec3 v0=calc(i,j),v1=calc(i+1,j),v2=calc(i,j+1),v3=calc(i+1,j+1);
        push(v0);push(v1);push(v2);
        push(v1);push(v3);push(v2);
    }
    nVertices = (int)vBuffer.size()/11;
    GLuint VBO, VAO;
    glGenBuffers(1,&VBO); glBindBuffer(GL_ARRAY_BUFFER,VBO);
    glBufferData(GL_ARRAY_BUFFER,vBuffer.size()*sizeof(GLfloat),vBuffer.data(),GL_STATIC_DRAW);
    glGenVertexArrays(1,&VAO); glBindVertexArray(VAO);
    const GLsizei S=11*sizeof(GLfloat);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,S,(GLvoid*)0);                  glEnableVertexAttribArray(0);
    glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,S,(GLvoid*)(3*sizeof(GLfloat))); glEnableVertexAttribArray(1);
    glVertexAttribPointer(2,3,GL_FLOAT,GL_FALSE,S,(GLvoid*)(6*sizeof(GLfloat))); glEnableVertexAttribArray(2);
    glVertexAttribPointer(3,2,GL_FLOAT,GL_FALSE,S,(GLvoid*)(9*sizeof(GLfloat))); glEnableVertexAttribArray(3);
    glBindBuffer(GL_ARRAY_BUFFER,0); glBindVertexArray(0);
    return VAO;
}
