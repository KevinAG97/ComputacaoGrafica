/* Modulo3 - Visualizador de modelos OBJ com texturas
 *
 * Módulo 3 - Computação Gráfica - Unisinos
 * Autor: Kevin de Azevedo Garcia
 *
 * Novidades em relação ao Vivencial2:
 *   - Leitura das coordenadas de textura (vt) do arquivo .OBJ
 *   - Leitura do arquivo .MTL para obter o nome da textura (map_Kd)
 *   - Carregamento da textura com stb_image
 *   - Buffer de vértices: x, y, z, s, t  (5 floats por vértice)
 *   - Shader com suporte a textura e fallback para cor sólida
 *
 * Controles:
 *   Tab        : selecionar próximo objeto
 *   X / Y / Z  : alternar rotação contínua no eixo correspondente
 *   W / S      : transladar no eixo Z (frente/trás)
 *   A / D      : transladar no eixo X (esquerda/direita)
 *   Seta ↑ / ↓ : transladar no eixo Y (cima/baixo)
 *   [ / ]      : diminuir / aumentar escala uniforme
 *   ESC        : sair
 *
 * O objeto selecionado é destacado com wireframe branco.
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

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

// =========================================================
// Shaders
//
// Atributo 0 (location 0): posição  (x, y, z)
// Atributo 1 (location 1): coord UV (s, t)
//
// Uniforms de textura:
//   useTexture : 1 = amostrar textura; 0 = usar flatColor
//   texBuff    : sampler2D ligado ao GL_TEXTURE0
//   flatColor  : cor de fallback para objetos sem textura
//   highlight  : 1 = sobrescreve com branco (wireframe do objeto selecionado)
// =========================================================
const GLchar* vertexShaderSource =
    "#version 450\n"
    "layout(location = 0) in vec3 position;\n"
    "layout(location = 1) in vec2 texCoord;\n"
    "uniform mat4 model;\n"
    "uniform mat4 view;\n"
    "uniform mat4 projection;\n"
    "out vec2 fragTexCoord;\n"
    "void main() {\n"
    "    gl_Position = projection * view * model * vec4(position, 1.0);\n"
    "    fragTexCoord = texCoord;\n"
    "}\0";

const GLchar* fragmentShaderSource =
    "#version 450\n"
    "in vec2 fragTexCoord;\n"
    "uniform sampler2D texBuff;\n"
    "uniform int useTexture;\n"
    "uniform vec3 flatColor;\n"
    "uniform int highlight;\n"
    "out vec4 color;\n"
    "void main() {\n"
    "    if (highlight == 1)\n"
    "        color = vec4(1.0, 1.0, 1.0, 1.0);\n"
    "    else if (useTexture == 1)\n"
    "        color = texture(texBuff, fragTexCoord);\n"
    "    else\n"
    "        color = vec4(flatColor, 1.0);\n"
    "}\n\0";

// =========================================================
// Struct de modelo OBJ na cena
// =========================================================
struct OBJModel
{
    GLuint    VAO;
    int       nVertices;
    GLuint    texID;      // 0 = sem textura
    glm::vec3 position;
    float     scale;
    bool      rotateX, rotateY, rotateZ;
    glm::vec3 color;      // cor de fallback quando texID == 0

    OBJModel()
        : VAO(0), nVertices(0), texID(0),
          position(0.0f), scale(1.0f),
          rotateX(false), rotateY(false), rotateZ(false),
          color(1.0f, 0.5f, 0.0f)
    {}
};

// =========================================================
// Globals
// =========================================================
const GLuint WIDTH = 1000, HEIGHT = 1000;

vector<OBJModel> objects;
int selectedObj = 0;

// =========================================================
// Protótipos
// =========================================================
void   key_callback(GLFWwindow* window, int key, int scancode, int action, int mode);
int    setupShader();
GLuint loadTexture(const string& filePath);
string loadMTL(const string& mtlPath);
GLuint loadSimpleOBJ(const string& filePath, int& nVertices, GLuint& texID);

// =========================================================
// main
// =========================================================
int main()
{
    glfwInit();

    GLFWwindow* window = glfwCreateWindow(WIDTH, HEIGHT,
        "Modulo 3 -- Kevin de Azevedo Garcia", nullptr, nullptr);
    glfwMakeContextCurrent(window);
    glfwSetKeyCallback(window, key_callback);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
        cout << "Failed to initialize GLAD" << endl;

    cout << "Renderer: " << glGetString(GL_RENDERER) << endl;
    cout << "OpenGL:   " << glGetString(GL_VERSION)  << endl;

    int w, h;
    glfwGetFramebufferSize(window, &w, &h);
    glViewport(0, 0, w, h);

    GLuint shaderID = setupShader();
    glUseProgram(shaderID);

    // --- Carregar modelos OBJ ---
    // Caminhos relativos ao diretório de build (build/)
    {
        OBJModel m;
        m.color    = glm::vec3(1.0f, 0.45f, 0.0f); // laranja (fallback)
        m.position = glm::vec3(-1.5f, 0.0f, 0.0f);
        m.scale    = 1.0f;
        m.VAO      = loadSimpleOBJ("../assets/Modelos3D/Suzanne.obj", m.nVertices, m.texID);
        if (m.VAO != 0) objects.push_back(m);
    }
    {
        OBJModel m;
        m.color    = glm::vec3(0.0f, 0.65f, 1.0f); // ciano (fallback)
        m.position = glm::vec3(1.5f, 0.0f, 0.0f);
        m.scale    = 1.0f;
        m.VAO      = loadSimpleOBJ("../assets/Modelos3D/Cube.obj", m.nVertices, m.texID);
        if (m.VAO != 0) objects.push_back(m);
    }

    if (objects.empty()) {
        cerr << "Nenhum modelo carregado! Verifique os caminhos dos arquivos .obj." << endl;
        glfwTerminate();
        return -1;
    }

    // --- Câmera fixa ---
    glm::mat4 view = glm::lookAt(
        glm::vec3(0.0f, 2.0f, 6.0f),
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f)
    );
    glm::mat4 projection = glm::perspective(
        glm::radians(45.0f),
        (float)WIDTH / (float)HEIGHT,
        0.1f, 100.0f
    );

    GLint modelLoc      = glGetUniformLocation(shaderID, "model");
    GLint viewLoc       = glGetUniformLocation(shaderID, "view");
    GLint projLoc       = glGetUniformLocation(shaderID, "projection");
    GLint highlightLoc  = glGetUniformLocation(shaderID, "highlight");
    GLint useTextureLoc = glGetUniformLocation(shaderID, "useTexture");
    GLint flatColorLoc  = glGetUniformLocation(shaderID, "flatColor");
    GLint texBuffLoc    = glGetUniformLocation(shaderID, "texBuff");

    glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(projection));

    // Textura sempre no slot 0
    glUniform1i(texBuffLoc, 0);
    glActiveTexture(GL_TEXTURE0);

    glEnable(GL_DEPTH_TEST);

    cout << "\n=== Controles ===" << endl;
    cout << "Tab      : selecionar proximo objeto" << endl;
    cout << "X/Y/Z    : alternar rotacao no eixo correspondente" << endl;
    cout << "W/S      : transladar no eixo Z (frente/tras)" << endl;
    cout << "A/D      : transladar no eixo X (esquerda/direita)" << endl;
    cout << "Seta↑/↓  : transladar no eixo Y (cima/baixo)" << endl;
    cout << "[ / ]    : diminuir / aumentar escala uniforme" << endl;
    cout << "ESC      : sair" << endl;
    cout << "\nObjeto selecionado: 0 (wireframe branco = selecionado)" << endl;

    // =========================================================
    // Loop principal
    // =========================================================
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        glClearColor(0.1f, 0.1f, 0.15f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        float angle = (float)glfwGetTime();

        for (int i = 0; i < (int)objects.size(); i++)
        {
            OBJModel& obj = objects[i];

            glm::mat4 model = glm::mat4(1.0f);
            model = glm::translate(model, obj.position);
            model = glm::scale(model, glm::vec3(obj.scale));

            if      (obj.rotateX) model = glm::rotate(model, angle, glm::vec3(1.0f, 0.0f, 0.0f));
            else if (obj.rotateY) model = glm::rotate(model, angle, glm::vec3(0.0f, 1.0f, 0.0f));
            else if (obj.rotateZ) model = glm::rotate(model, angle, glm::vec3(0.0f, 0.0f, 1.0f));

            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));

            // Configurar textura ou cor sólida
            if (obj.texID != 0) {
                glUniform1i(useTextureLoc, 1);
                glBindTexture(GL_TEXTURE_2D, obj.texID);
            } else {
                glUniform1i(useTextureLoc, 0);
                glUniform3fv(flatColorLoc, 1, glm::value_ptr(obj.color));
            }

            glBindVertexArray(obj.VAO);

            // Preenchimento sólido (recuado para evitar z-fighting com wireframe)
            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(1.0f, 1.0f);
            glUniform1i(highlightLoc, 0);
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
            glDrawArrays(GL_TRIANGLES, 0, obj.nVertices);
            glDisable(GL_POLYGON_OFFSET_FILL);

            // Wireframe branco sobre o objeto selecionado
            if (i == selectedObj)
            {
                glUniform1i(highlightLoc, 1);
                glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
                glDrawArrays(GL_TRIANGLES, 0, obj.nVertices);
                glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
            }

            glBindVertexArray(0);
            glBindTexture(GL_TEXTURE_2D, 0);
        }

        glfwSwapBuffers(window);
    }

    for (auto& obj : objects) {
        glDeleteVertexArrays(1, &obj.VAO);
        if (obj.texID != 0)
            glDeleteTextures(1, &obj.texID);
    }
    glfwTerminate();
    return 0;
}

// =========================================================
// Callback de teclado
// =========================================================
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mode)
{
    const float MOVE_STEP  = 0.1f;
    const float SCALE_STEP = 0.05f;
    const float MIN_SCALE  = 0.1f;
    const float MAX_SCALE  = 5.0f;

    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
    {
        glfwSetWindowShouldClose(window, GL_TRUE);
        return;
    }

    if (objects.empty()) return;

    if (key == GLFW_KEY_TAB && action == GLFW_PRESS)
    {
        selectedObj = (selectedObj + 1) % (int)objects.size();
        cout << "Objeto selecionado: " << selectedObj << endl;
        return;
    }

    OBJModel& obj = objects[selectedObj];

    if (key == GLFW_KEY_X && action == GLFW_PRESS)
    {
        obj.rotateX = !obj.rotateX;
        obj.rotateY = false;
        obj.rotateZ = false;
    }
    if (key == GLFW_KEY_Y && action == GLFW_PRESS)
    {
        obj.rotateX = false;
        obj.rotateY = !obj.rotateY;
        obj.rotateZ = false;
    }
    if (key == GLFW_KEY_Z && action == GLFW_PRESS)
    {
        obj.rotateX = false;
        obj.rotateY = false;
        obj.rotateZ = !obj.rotateZ;
    }

    if (key == GLFW_KEY_W && (action == GLFW_PRESS || action == GLFW_REPEAT))
        obj.position.z -= MOVE_STEP;
    if (key == GLFW_KEY_S && (action == GLFW_PRESS || action == GLFW_REPEAT))
        obj.position.z += MOVE_STEP;
    if (key == GLFW_KEY_A && (action == GLFW_PRESS || action == GLFW_REPEAT))
        obj.position.x -= MOVE_STEP;
    if (key == GLFW_KEY_D && (action == GLFW_PRESS || action == GLFW_REPEAT))
        obj.position.x += MOVE_STEP;

    if (key == GLFW_KEY_UP   && (action == GLFW_PRESS || action == GLFW_REPEAT))
        obj.position.y += MOVE_STEP;
    if (key == GLFW_KEY_DOWN && (action == GLFW_PRESS || action == GLFW_REPEAT))
        obj.position.y -= MOVE_STEP;

    if (key == GLFW_KEY_LEFT_BRACKET  && (action == GLFW_PRESS || action == GLFW_REPEAT))
        obj.scale = max(MIN_SCALE, obj.scale - SCALE_STEP);
    if (key == GLFW_KEY_RIGHT_BRACKET && (action == GLFW_PRESS || action == GLFW_REPEAT))
        obj.scale = min(MAX_SCALE, obj.scale + SCALE_STEP);
}

// =========================================================
// Compilar e linkar shaders
// =========================================================
int setupShader()
{
    GLint  ok;
    GLchar log[512];

    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &vertexShaderSource, NULL);
    glCompileShader(vs);
    glGetShaderiv(vs, GL_COMPILE_STATUS, &ok);
    if (!ok) { glGetShaderInfoLog(vs, 512, NULL, log); cout << "VS ERROR: " << log << endl; }

    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &fragmentShaderSource, NULL);
    glCompileShader(fs);
    glGetShaderiv(fs, GL_COMPILE_STATUS, &ok);
    if (!ok) { glGetShaderInfoLog(fs, 512, NULL, log); cout << "FS ERROR: " << log << endl; }

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) { glGetProgramInfoLog(prog, 512, NULL, log); cout << "LINK ERROR: " << log << endl; }

    glDeleteShader(vs);
    glDeleteShader(fs);
    return prog;
}

// =========================================================
// Carrega imagem e cria textura OpenGL
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
// Lê arquivo .MTL e retorna o nome do arquivo de textura
// difusa (map_Kd). Retorna string vazia se não encontrado.
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
// Carrega arquivo .OBJ com coordenadas de textura
//
// Processo:
//   1. Lê "mtllib" para encontrar o arquivo .MTL
//   2. Chama loadMTL para obter o nome da textura (map_Kd)
//   3. Chama loadTexture para carregar a imagem
//   4. Monta buffer: x, y, z, s, t  (5 floats por vértice)
//   5. Cria VAO/VBO com dois atributos:
//        location 0 -> posição  (xyz, stride 5*float, offset 0)
//        location 1 -> texCoord (st,  stride 5*float, offset 3*float)
//
// Retorna VAO; preenche nVertices e texID por referência.
// =========================================================
GLuint loadSimpleOBJ(const string& filePath, int& nVertices, GLuint& texID)
{
    texID = 0;

    vector<glm::vec3> positions;
    vector<glm::vec2> texCoords;
    vector<glm::vec3> normals;
    vector<GLfloat>   vBuffer;

    // Diretório base do OBJ (para resolver MTL e textura com caminho relativo)
    string dir = "";
    size_t slash = filePath.find_last_of("/\\");
    if (slash != string::npos)
        dir = filePath.substr(0, slash + 1);

    string mtlFile = "";

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
            // Nome do arquivo MTL referenciado pelo OBJ
            ss >> mtlFile;
        }
        else if (word == "v") {
            glm::vec3 v;
            ss >> v.x >> v.y >> v.z;
            positions.push_back(v);
        }
        else if (word == "vt") {
            glm::vec2 vt;
            ss >> vt.s >> vt.t;
            texCoords.push_back(vt);
        }
        else if (word == "vn") {
            glm::vec3 vn;
            ss >> vn.x >> vn.y >> vn.z;
            normals.push_back(vn);
        }
        else if (word == "f")
        {
            // Cada token: vi/ti/ni (índices 1-based no .OBJ)
            struct FaceVert { int vi, ti, ni; };
            vector<FaceVert> faceVerts;

            while (ss >> word)
            {
                FaceVert fv = { 0, -1, 0 };
                istringstream face(word);
                string idx;
                if (getline(face, idx, '/')) fv.vi = idx.empty() ? 0 : stoi(idx) - 1;
                if (getline(face, idx, '/')) fv.ti = idx.empty() ? -1 : stoi(idx) - 1;
                if (getline(face, idx))      fv.ni = idx.empty() ? 0  : stoi(idx) - 1;
                faceVerts.push_back(fv);
            }

            // Triangulação em fan: (v0,v1,v2), (v0,v2,v3), ...
            for (int k = 1; k + 1 < (int)faceVerts.size(); k++)
            {
                int tris[3] = { 0, k, k + 1 };
                for (int t : tris)
                {
                    const FaceVert& fv = faceVerts[t];

                    vBuffer.push_back(positions[fv.vi].x);
                    vBuffer.push_back(positions[fv.vi].y);
                    vBuffer.push_back(positions[fv.vi].z);

                    // Coordenada de textura (s, t) — (0,0) se ausente
                    if (fv.ti >= 0 && fv.ti < (int)texCoords.size()) {
                        vBuffer.push_back(texCoords[fv.ti].s);
                        vBuffer.push_back(texCoords[fv.ti].t);
                    } else {
                        vBuffer.push_back(0.0f);
                        vBuffer.push_back(0.0f);
                    }
                }
            }
        }
    }
    file.close();

    // --- Resolver textura via MTL ---
    if (!mtlFile.empty()) {
        string texName = loadMTL(dir + mtlFile);
        if (!texName.empty())
            texID = loadTexture(dir + texName);
    }

    // 5 valores por vértice: x, y, z, s, t
    nVertices = (int)vBuffer.size() / 5;
    cout << "Modelo carregado: " << filePath << " (" << nVertices << " vertices)" << endl;

    GLuint VBO, VAO;
    glGenBuffers(1, &VBO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, vBuffer.size() * sizeof(GLfloat), vBuffer.data(), GL_STATIC_DRAW);

    glGenVertexArrays(1, &VAO);
    glBindVertexArray(VAO);

    // Atributo 0: posição (x, y, z) — 3 floats, stride 5, offset 0
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), (GLvoid*)0);
    glEnableVertexAttribArray(0);

    // Atributo 1: coordenada de textura (s, t) — 2 floats, stride 5, offset 3
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), (GLvoid*)(3 * sizeof(GLfloat)));
    glEnableVertexAttribArray(1);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    return VAO;
}
