/* VivencialM4 - Iluminação de Três Pontos com modelo OBJ e textura
 *
 * Módulo 4 - Computação Gráfica - Unisinos
 * Autor: Kevin de Azevedo Garcia
 *
 * Implementa a técnica cinematográfica de iluminação de três pontos sobre um
 * modelo OBJ, com iluminação de Phong, fator de atenuação na parcela difusa
 * e suporte a textura difusa lida do arquivo MTL (map_Kd).
 *
 * Quando o modelo possui textura, ela é usada como cor do objeto na equação
 * de Phong. Caso contrário, usa-se a cor sólida definida em OBJModel::color.
 *
 * As três luzes são posicionadas automaticamente em relação à posição e
 * escala do objeto principal:
 *   - Key Light  (1) : principal, mais intensa, frente-esquerda, levemente acima
 *   - Fill Light (2) : preenchimento, ~50% da key, frente-direita
 *   - Back Light (3) : contra-luz, atrás e acima, separa o objeto do fundo
 *
 * Controles:
 *   1 / 2 / 3  : habilitar / desabilitar Key / Fill / Back light
 *   X / Y / Z  : alternar rotação contínua no eixo correspondente
 *   W / S      : transladar no eixo Z (frente / trás)
 *   A / D      : transladar no eixo X (esquerda / direita)
 *   Seta↑ / ↓  : transladar no eixo Y (cima / baixo)
 *   [ / ]      : diminuir / aumentar escala uniforme
 *   ESC        : sair
 */

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>

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
//    location 0 : posição  (x, y, z)       offset  0
//    location 1 : cor      (r, g, b)       offset  3
//    location 2 : normal   (nx, ny, nz)    offset  6
//    location 3 : texcoord (s, t)          offset  9
//
//  A normal é transformada pelo inverso-transposto da matriz modelo
//  para permanecer correta sob rotações e escalas uniformes.
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
    vec4 worldPos   = model * vec4(position, 1.0);
    gl_Position     = projection * view * worldPos;
    fragPos         = vec3(worldPos);
    vNormal         = mat3(transpose(inverse(model))) * normal;
    vColor          = color;
    fragTexCoord    = texCoord;
}
)";

// =========================================================
//  Fragment Shader — Phong com 3 luzes pontuais e textura
//
//  Cada PointLight:
//    position               : posição no espaço mundo
//    color                  : cor RGB da luz
//    intensity              : escalar de potência
//    enabled                : 1 = ativa, 0 = desligada
//    constant/linear/quad   : coeficientes de atenuação
//
//  Atenuação aplicada APENAS na parcela difusa (requisito do exercício).
//
//  Cor do objeto:
//    useTexture == 1  → amostra texBuff
//    useTexture == 0  → usa vColor (cor sólida)
// =========================================================
const GLchar* fragmentShaderSource = R"(
#version 450

in vec3  vNormal;
in vec3  fragPos;
in vec3  vColor;
in vec2  fragTexCoord;

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

uniform PointLight    lights[3];
uniform sampler2D     texBuff;
uniform int           useTexture;
uniform vec3          camPos;
uniform float         ka;
uniform float         kd;
uniform float         ks;
uniform float         q;

void main()
{
    // Cor base do objeto: textura ou cor sólida
    vec3 objColor = (useTexture == 1)
                    ? vec3(texture(texBuff, fragTexCoord))
                    : vColor;

    vec3 N = normalize(vNormal);
    vec3 V = normalize(camPos - fragPos);

    // Componente ambiente (global, independente das luzes)
    vec3 result = ka * objColor;

    for (int i = 0; i < 3; i++)
    {
        if (lights[i].enabled == 0) continue;

        vec3  L    = normalize(lights[i].position - fragPos);
        float dist = length(lights[i].position - fragPos);

        // ---- Fator de atenuação — aplicado apenas ao difuso ----
        float attenuation = 1.0 / (lights[i].constant
                                 + lights[i].linear    * dist
                                 + lights[i].quadratic * dist * dist);

        // ---- Parcela difusa (com atenuação) ----
        float diff    = max(dot(N, L), 0.0);
        vec3  diffuse = kd * diff * lights[i].color * lights[i].intensity
                        * attenuation;

        // ---- Parcela especular — Phong (sem atenuação) ----
        vec3  R    = normalize(reflect(-L, N));
        float spec = pow(max(dot(R, V), 0.0), q);
        vec3  specular = ks * spec * lights[i].color * lights[i].intensity;

        // Difuso multiplica pela cor do objeto; especular não
        result += diffuse * objColor + specular;
    }

    color = vec4(result, 1.0);
}
)";

// =========================================================
//  Estruturas de dados
// =========================================================

struct OBJModel
{
    GLuint VAO       = 0;
    int    nVertices = 0;
    GLuint texID     = 0;       // 0 = sem textura (usa color abaixo)
    vec3   position  = vec3(0.0f);
    float  scale     = 1.0f;
    bool   rotateX   = false;
    bool   rotateY   = false;
    bool   rotateZ   = false;
    vec3   color     = vec3(0.85f, 0.72f, 0.60f); // bege — bom para ver Phong sem textura
};

struct PointLight
{
    vec3   position;
    vec3   color;
    float  intensity;
    bool   enabled;
    float  constant;
    float  linear;
    float  quadratic;
    string name;
};

// =========================================================
//  Globals
// =========================================================
const GLuint WIDTH = 1000, HEIGHT = 1000;

OBJModel   gModel;
PointLight gLights[3];

const vec3 CAM_POS = vec3(0.0f, 1.5f, 5.0f);

// =========================================================
//  Protótipos
// =========================================================
void   key_callback(GLFWwindow* window, int key, int scancode, int action, int mode);
int    setupShader();
GLuint loadTexture(const string& filePath);
string loadMTL(const string& mtlPath);
GLuint loadOBJWithNormals(const string& filePath, int& nVertices, GLuint& texID, vec3 fallbackColor);
GLuint generateSphereSimple(float radius, int latSeg, int lonSeg, int& nVertices, vec3 color);
void   sendLightUniforms(GLuint shader);
void   updateLightPositions();

// =========================================================
//  Posicionamento automático das luzes
//  Todas as posições derivam do centro e escala do objeto.
// =========================================================
void updateLightPositions()
{
    const vec3& c = gModel.position;
    const float  r = gModel.scale;

    // Key Light  : frente-esquerda, levemente acima (45° lateral, 30° vertical)
    gLights[0].position = c + vec3(-1.5f, 1.0f, 2.0f) * r;

    // Fill Light : frente-direita, altura intermediária (oposta à key)
    gLights[1].position = c + vec3( 1.8f, 0.5f, 1.8f) * r;

    // Back Light : atrás e acima (rim light — separa do fundo)
    gLights[2].position = c + vec3( 0.3f, 2.2f,-1.8f) * r;
}

// =========================================================
//  Envia as 3 luzes ao fragment shader
// =========================================================
void sendLightUniforms(GLuint shader)
{
    char buf[64];
    for (int i = 0; i < 3; i++)
    {
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
//  main
// =========================================================
int main()
{
    glfwInit();
    GLFWwindow* window = glfwCreateWindow(WIDTH, HEIGHT,
        "VivencialM4 - Iluminacao 3 Pontos -- Kevin Garcia", nullptr, nullptr);
    glfwMakeContextCurrent(window);
    glfwSetKeyCallback(window, key_callback);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
        cout << "Failed to initialize GLAD" << endl;

    cout << "Renderer : " << glGetString(GL_RENDERER) << endl;
    cout << "OpenGL   : " << glGetString(GL_VERSION)  << endl;

    int w, h;
    glfwGetFramebufferSize(window, &w, &h);
    glViewport(0, 0, w, h);

    GLuint shaderID = setupShader();
    glUseProgram(shaderID);

    // --- Carregar modelo principal ---
    gModel.color    = vec3(0.85f, 0.72f, 0.60f);
    gModel.position = vec3(0.0f);
    gModel.scale    = 1.0f;
    gModel.VAO      = loadOBJWithNormals("../assets/Modelos3D/Suzanne.obj",
                                          gModel.nVertices, gModel.texID, gModel.color);
    if (gModel.VAO == 0) {
        cerr << "Falha ao carregar modelo. Encerrando." << endl;
        glfwTerminate();
        return -1;
    }

    // --- Configurar as três luzes ---
    gLights[0] = { {}, vec3(1.00f, 0.95f, 0.80f), 1.0f, true,  1.0f, 0.14f, 0.07f, "Key Light  (1)" };
    gLights[1] = { {}, vec3(0.70f, 0.80f, 1.00f), 0.5f, true,  1.0f, 0.22f, 0.20f, "Fill Light (2)" };
    gLights[2] = { {}, vec3(0.90f, 0.90f, 1.00f), 0.7f, true,  1.0f, 0.18f, 0.10f, "Back Light (3)" };

    updateLightPositions();

    // --- Esferas marcadoras de luz ---
    int    nSphereVerts;
    GLuint sphereVAO[3];
    for (int i = 0; i < 3; i++)
        sphereVAO[i] = generateSphereSimple(0.06f, 10, 10, nSphereVerts, gLights[i].color);

    // --- Câmera ---
    mat4 view       = lookAt(CAM_POS, vec3(0.0f), vec3(0.0f, 1.0f, 0.0f));
    mat4 projection = perspective(radians(45.0f), (float)WIDTH / HEIGHT, 0.1f, 100.0f);

    GLint modelLoc      = glGetUniformLocation(shaderID, "model");
    GLint viewLoc       = glGetUniformLocation(shaderID, "view");
    GLint projLoc       = glGetUniformLocation(shaderID, "projection");
    GLint camLoc        = glGetUniformLocation(shaderID, "camPos");
    GLint useTextureLoc = glGetUniformLocation(shaderID, "useTexture");
    GLint texBuffLoc    = glGetUniformLocation(shaderID, "texBuff");

    glUniformMatrix4fv(viewLoc, 1, GL_FALSE, value_ptr(view));
    glUniformMatrix4fv(projLoc, 1, GL_FALSE, value_ptr(projection));
    glUniform3fv(camLoc, 1, value_ptr(CAM_POS));

    // Textura sempre no slot 0
    glUniform1i(texBuffLoc, 0);
    glActiveTexture(GL_TEXTURE0);

    // Coeficientes de Phong
    glUniform1f(glGetUniformLocation(shaderID, "ka"),  0.10f);
    glUniform1f(glGetUniformLocation(shaderID, "kd"),  0.70f);
    glUniform1f(glGetUniformLocation(shaderID, "ks"),  0.50f);
    glUniform1f(glGetUniformLocation(shaderID, "q"),  32.0f);

    glEnable(GL_DEPTH_TEST);

    cout << "\n=== Controles ===" << endl;
    cout << "1 / 2 / 3  : ligar ou desligar Key / Fill / Back light" << endl;
    cout << "X / Y / Z  : alternar rotacao continua no eixo" << endl;
    cout << "W / S      : transladar Z  |  A / D : transladar X" << endl;
    cout << "Seta↑ / ↓  : transladar Y  |  [ / ] : escala uniforme" << endl;
    cout << "ESC        : sair" << endl;
    cout << "\nStatus inicial: todas as luzes LIGADAS" << endl;

    // =========================================================
    //  Loop principal
    // =========================================================
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        glClearColor(0.05f, 0.05f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        float angle = (float)glfwGetTime();

        updateLightPositions();
        sendLightUniforms(shaderID);

        // ---------------------------------------------------------
        //  Desenhar o modelo principal com Phong + textura
        // ---------------------------------------------------------
        {
            mat4 model = mat4(1.0f);
            model = translate(model, gModel.position);
            model = scale(model, vec3(gModel.scale));
            if      (gModel.rotateX) model = rotate(model, angle, vec3(1,0,0));
            else if (gModel.rotateY) model = rotate(model, angle, vec3(0,1,0));
            else if (gModel.rotateZ) model = rotate(model, angle, vec3(0,0,1));

            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, value_ptr(model));

            // Restaurar coeficientes Phong normais (podem ter sido alterados pelos marcadores)
            glUniform1f(glGetUniformLocation(shaderID, "ka"),  0.10f);
            glUniform1f(glGetUniformLocation(shaderID, "kd"),  0.70f);
            glUniform1f(glGetUniformLocation(shaderID, "ks"),  0.50f);

            // Configurar textura ou cor sólida
            if (gModel.texID != 0) {
                glUniform1i(useTextureLoc, 1);
                glBindTexture(GL_TEXTURE_2D, gModel.texID);
            } else {
                glUniform1i(useTextureLoc, 0);
                glBindTexture(GL_TEXTURE_2D, 0);
            }

            glBindVertexArray(gModel.VAO);
            glDrawArrays(GL_TRIANGLES, 0, gModel.nVertices);
            glBindVertexArray(0);
            glBindTexture(GL_TEXTURE_2D, 0);
        }

        // ---------------------------------------------------------
        //  Desenhar marcadores das luzes (esferas emissivas)
        //  ka = 1, kd = ks = 0 → cor pura sem sombreamento
        // ---------------------------------------------------------
        glUniform1i(useTextureLoc, 0);
        glUniform1f(glGetUniformLocation(shaderID, "ka"),  1.0f);
        glUniform1f(glGetUniformLocation(shaderID, "kd"),  0.0f);
        glUniform1f(glGetUniformLocation(shaderID, "ks"),  0.0f);

        for (int i = 0; i < 3; i++)
        {
            if (!gLights[i].enabled) continue;

            mat4 model = translate(mat4(1.0f), gLights[i].position);
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, value_ptr(model));

            glBindVertexArray(sphereVAO[i]);
            glDrawArrays(GL_TRIANGLES, 0, nSphereVerts);
            glBindVertexArray(0);
        }

        glfwSwapBuffers(window);
    }

    glDeleteVertexArrays(1, &gModel.VAO);
    if (gModel.texID) glDeleteTextures(1, &gModel.texID);
    for (int i = 0; i < 3; i++) glDeleteVertexArrays(1, &sphereVAO[i]);
    glfwTerminate();
    return 0;
}

// =========================================================
//  Callback de teclado
// =========================================================
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mode)
{
    const float MOVE  = 0.1f;
    const float SCALE = 0.05f;

    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, GL_TRUE);
        return;
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

    // Rotação (toggle: mesma tecla desliga)
    if (key == GLFW_KEY_X && action == GLFW_PRESS) {
        gModel.rotateX = !gModel.rotateX; gModel.rotateY = false; gModel.rotateZ = false;
    }
    if (key == GLFW_KEY_Y && action == GLFW_PRESS) {
        gModel.rotateX = false; gModel.rotateY = !gModel.rotateY; gModel.rotateZ = false;
    }
    if (key == GLFW_KEY_Z && action == GLFW_PRESS) {
        gModel.rotateX = false; gModel.rotateY = false; gModel.rotateZ = !gModel.rotateZ;
    }

    // Translação
    if (key == GLFW_KEY_W    && (action == GLFW_PRESS || action == GLFW_REPEAT)) gModel.position.z -= MOVE;
    if (key == GLFW_KEY_S    && (action == GLFW_PRESS || action == GLFW_REPEAT)) gModel.position.z += MOVE;
    if (key == GLFW_KEY_A    && (action == GLFW_PRESS || action == GLFW_REPEAT)) gModel.position.x -= MOVE;
    if (key == GLFW_KEY_D    && (action == GLFW_PRESS || action == GLFW_REPEAT)) gModel.position.x += MOVE;
    if (key == GLFW_KEY_UP   && (action == GLFW_PRESS || action == GLFW_REPEAT)) gModel.position.y += MOVE;
    if (key == GLFW_KEY_DOWN && (action == GLFW_PRESS || action == GLFW_REPEAT)) gModel.position.y -= MOVE;

    // Escala uniforme
    if (key == GLFW_KEY_LEFT_BRACKET  && (action == GLFW_PRESS || action == GLFW_REPEAT))
        gModel.scale = std::max(0.1f, gModel.scale - SCALE);
    if (key == GLFW_KEY_RIGHT_BRACKET && (action == GLFW_PRESS || action == GLFW_REPEAT))
        gModel.scale = std::min(5.0f, gModel.scale + SCALE);
}

// =========================================================
//  Compilar e linkar shaders
// =========================================================
int setupShader()
{
    GLint  ok; GLchar log[512];

    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &vertexShaderSource, NULL);
    glCompileShader(vs);
    glGetShaderiv(vs, GL_COMPILE_STATUS, &ok);
    if (!ok) { glGetShaderInfoLog(vs, 512, NULL, log); cerr << "VS ERROR:\n" << log << endl; }

    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &fragmentShaderSource, NULL);
    glCompileShader(fs);
    glGetShaderiv(fs, GL_COMPILE_STATUS, &ok);
    if (!ok) { glGetShaderInfoLog(fs, 512, NULL, log); cerr << "FS ERROR:\n" << log << endl; }

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) { glGetProgramInfoLog(prog, 512, NULL, log); cerr << "LINK ERROR:\n" << log << endl; }

    glDeleteShader(vs);
    glDeleteShader(fs);
    return prog;
}

// =========================================================
//  Carrega imagem e cria textura OpenGL (igual ao Modulo3)
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

    int width, height, nrChannels;
    stbi_set_flip_vertically_on_load(true);
    unsigned char* data = stbi_load(filePath.c_str(), &width, &height, &nrChannels, 0);

    if (data) {
        GLenum fmt = (nrChannels == 4) ? GL_RGBA : GL_RGB;
        glTexImage2D(GL_TEXTURE_2D, 0, fmt, width, height, 0, fmt, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);
        cout << "Textura carregada: " << filePath
             << " (" << width << "x" << height << ", " << nrChannels << " canais)" << endl;
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
//  Lê arquivo .MTL e retorna o nome do arquivo de textura
//  difusa (map_Kd). Retorna string vazia se não encontrado.
// =========================================================
string loadMTL(const string& mtlPath)
{
    ifstream file(mtlPath);
    if (!file.is_open()) {
        cerr << "Aviso: MTL nao encontrado: " << mtlPath << endl;
        return "";
    }
    string line;
    while (getline(file, line)) {
        istringstream ss(line);
        string keyword;
        ss >> keyword;
        if (keyword == "map_Kd") {
            string texName;
            ss >> texName;
            return texName;
        }
    }
    return "";
}

// =========================================================
//  Carrega .OBJ com normais e coordenadas de textura
//
//  Buffer de saída: 11 floats por vértice
//    x  y  z  |  r  g  b  |  nx  ny  nz  |  s  t
//    loc 0       loc 1        loc 2           loc 3
//
//  Processo:
//    1. Lê "mtllib" → chama loadMTL → chama loadTexture
//    2. Expande faces com fan triangulation
//    3. Cria VAO/VBO com os 4 atributos
//  Retorna VAO; preenche nVertices e texID por referência.
// =========================================================
GLuint loadOBJWithNormals(const string& filePath, int& nVertices,
                           GLuint& texID, vec3 fallbackColor)
{
    texID = 0;

    vector<vec3>    positions;
    vector<vec2>    texCoords;
    vector<vec3>    normals;
    vector<GLfloat> vBuffer;

    // Diretório base para resolver MTL e textura
    string dir;
    size_t slash = filePath.find_last_of("/\\");
    if (slash != string::npos)
        dir = filePath.substr(0, slash + 1);

    string mtlFile;

    ifstream file(filePath);
    if (!file.is_open()) {
        cerr << "Erro ao abrir: " << filePath << endl;
        return 0;
    }

    string line;
    while (getline(file, line))
    {
        istringstream ss(line);
        string word;
        ss >> word;

        if (word == "mtllib") {
            ss >> mtlFile;
        }
        else if (word == "v") {
            vec3 v; ss >> v.x >> v.y >> v.z;
            positions.push_back(v);
        }
        else if (word == "vt") {
            vec2 vt; ss >> vt.s >> vt.t;
            texCoords.push_back(vt);
        }
        else if (word == "vn") {
            vec3 vn; ss >> vn.x >> vn.y >> vn.z;
            normals.push_back(vn);
        }
        else if (word == "f")
        {
            struct FaceVert { int vi, ti, ni; };
            vector<FaceVert> fv;

            while (ss >> word) {
                FaceVert f = { 0, -1, -1 };
                istringstream face(word);
                string idx;
                if (getline(face, idx, '/')) f.vi = idx.empty() ? 0 : stoi(idx) - 1;
                if (getline(face, idx, '/')) f.ti = idx.empty() ? -1 : stoi(idx) - 1;
                if (getline(face, idx))      f.ni = idx.empty() ? -1 : stoi(idx) - 1;
                fv.push_back(f);
            }

            // Fan triangulation: (0,k,k+1) para k = 1..n-2
            for (int k = 1; k + 1 < (int)fv.size(); k++)
            {
                int tris[3] = { 0, k, k + 1 };
                for (int t : tris)
                {
                    int vi = fv[t].vi;
                    int ti = fv[t].ti;
                    int ni = fv[t].ni;

                    // Posição
                    vBuffer.push_back(positions[vi].x);
                    vBuffer.push_back(positions[vi].y);
                    vBuffer.push_back(positions[vi].z);
                    // Cor (fallback — substituída pela textura no shader quando texID != 0)
                    vBuffer.push_back(fallbackColor.r);
                    vBuffer.push_back(fallbackColor.g);
                    vBuffer.push_back(fallbackColor.b);
                    // Normal
                    if (ni >= 0 && ni < (int)normals.size()) {
                        vBuffer.push_back(normals[ni].x);
                        vBuffer.push_back(normals[ni].y);
                        vBuffer.push_back(normals[ni].z);
                    } else {
                        vBuffer.push_back(0.0f);
                        vBuffer.push_back(1.0f);
                        vBuffer.push_back(0.0f);
                    }
                    // Coordenada de textura
                    if (ti >= 0 && ti < (int)texCoords.size()) {
                        vBuffer.push_back(texCoords[ti].s);
                        vBuffer.push_back(texCoords[ti].t);
                    } else {
                        vBuffer.push_back(0.0f);
                        vBuffer.push_back(0.0f);
                    }
                }
            }
        }
    }
    file.close();

    // Resolver textura via MTL
    if (!mtlFile.empty()) {
        string texName = loadMTL(dir + mtlFile);
        if (!texName.empty())
            texID = loadTexture(dir + texName);
    }

    nVertices = (int)vBuffer.size() / 11;
    cout << "Modelo carregado: " << filePath
         << "  (" << nVertices << " vertices)"
         << (texID ? "  [com textura]" : "  [sem textura]") << endl;

    GLuint VBO, VAO;
    glGenBuffers(1, &VBO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, vBuffer.size() * sizeof(GLfloat), vBuffer.data(), GL_STATIC_DRAW);

    glGenVertexArrays(1, &VAO);
    glBindVertexArray(VAO);

    const GLsizei stride = 11 * sizeof(GLfloat);
    // location 0 : posição  (3 floats, offset 0)
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (GLvoid*)0);
    glEnableVertexAttribArray(0);
    // location 1 : cor      (3 floats, offset 3)
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (GLvoid*)(3 * sizeof(GLfloat)));
    glEnableVertexAttribArray(1);
    // location 2 : normal   (3 floats, offset 6)
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, stride, (GLvoid*)(6 * sizeof(GLfloat)));
    glEnableVertexAttribArray(2);
    // location 3 : texcoord (2 floats, offset 9)
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, stride, (GLvoid*)(9 * sizeof(GLfloat)));
    glEnableVertexAttribArray(3);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    return VAO;
}

// =========================================================
//  Gera esfera procedural (marcador de luz)
//  Buffer: x y z  r g b  nx ny nz  s t  (11 floats/vértice)
//  Mesma convenção do OBJ loader — mesmo VAO layout.
//  Coordenadas de textura (s,t) = 0 pois esferas não têm textura.
// =========================================================
GLuint generateSphereSimple(float radius, int latSeg, int lonSeg,
                             int& nVertices, vec3 color)
{
    vector<GLfloat> vBuffer;

    auto pushVert = [&](vec3 pos) {
        vec3 n = normalize(pos);
        // pos, color, normal, uv(0,0)
        vBuffer.insert(vBuffer.end(),
            { pos.x, pos.y, pos.z,
              color.r, color.g, color.b,
              n.x, n.y, n.z,
              0.0f, 0.0f });
    };

    auto calcPos = [&](int lat, int lon) -> vec3 {
        float theta = lat * pi<float>() / latSeg;
        float phi   = lon * 2.0f * pi<float>() / lonSeg;
        return vec3(radius * cos(phi) * sin(theta),
                    radius * cos(theta),
                    radius * sin(phi) * sin(theta));
    };

    for (int i = 0; i < latSeg; i++) {
        for (int j = 0; j < lonSeg; j++) {
            vec3 v0 = calcPos(i,   j  );
            vec3 v1 = calcPos(i+1, j  );
            vec3 v2 = calcPos(i,   j+1);
            vec3 v3 = calcPos(i+1, j+1);
            pushVert(v0); pushVert(v1); pushVert(v2);
            pushVert(v1); pushVert(v3); pushVert(v2);
        }
    }

    nVertices = (int)vBuffer.size() / 11;

    GLuint VBO, VAO;
    glGenBuffers(1, &VBO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, vBuffer.size() * sizeof(GLfloat), vBuffer.data(), GL_STATIC_DRAW);

    glGenVertexArrays(1, &VAO);
    glBindVertexArray(VAO);

    const GLsizei stride = 11 * sizeof(GLfloat);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (GLvoid*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (GLvoid*)(3 * sizeof(GLfloat)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, stride, (GLvoid*)(6 * sizeof(GLfloat)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, stride, (GLvoid*)(9 * sizeof(GLfloat)));
    glEnableVertexAttribArray(3);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    return VAO;
}
