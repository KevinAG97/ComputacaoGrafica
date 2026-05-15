/* Vivencial2 - Visualizador de modelos OBJ com seleção e transformações
 *
 * Baseado em Cubos3D.cpp
 * Módulo 2 - Computação Gráfica - Unisinos
 * Autor: Kevin de Azevedo Garcia
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

// =========================================================
// Shaders
// =========================================================
const GLchar* vertexShaderSource =
    "#version 450\n"
    "layout(location = 0) in vec3 position;\n"
    "layout(location = 1) in vec3 color;\n"
    "uniform mat4 model;\n"
    "uniform mat4 view;\n"
    "uniform mat4 projection;\n"
    "out vec4 finalColor;\n"
    "void main() {\n"
    "    gl_Position = projection * view * model * vec4(position, 1.0);\n"
    "    finalColor = vec4(color, 1.0);\n"
    "}\0";

const GLchar* fragmentShaderSource =
    "#version 450\n"
    "in vec4 finalColor;\n"
    "out vec4 color;\n"
    "uniform int highlight;\n"
    "void main() {\n"
    "    if (highlight == 1)\n"
    "        color = vec4(1.0, 1.0, 1.0, 1.0);\n"
    "    else\n"
    "        color = finalColor;\n"
    "}\n\0";

// =========================================================
// Struct representando um modelo OBJ na cena
// =========================================================
struct OBJModel
{
    GLuint    VAO;
    int       nVertices;
    glm::vec3 position;
    float     scale;
    bool      rotateX, rotateY, rotateZ;
    glm::vec3 color;

    OBJModel()
        : VAO(0), nVertices(0),
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
GLuint loadSimpleOBJ(const string& filePath, int& nVertices, glm::vec3 color);

// =========================================================
// main
// =========================================================
int main()
{
    glfwInit();

    GLFWwindow* window = glfwCreateWindow(WIDTH, HEIGHT,
        "Vivencial 2 -- Kevin de Azevedo Garcia", nullptr, nullptr);
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
    // Os caminhos são relativos ao diretório de build (build/)
    {
        OBJModel m;
        m.color    = glm::vec3(1.0f, 0.45f, 0.0f); // laranja
        m.position = glm::vec3(-1.5f, 0.0f, 0.0f);
        m.scale    = 1.0f;
        m.VAO      = loadSimpleOBJ("../assets/Modelos3D/Suzanne.obj", m.nVertices, m.color);
        if (m.VAO != 0) objects.push_back(m);
    }
    {
        OBJModel m;
        m.color    = glm::vec3(0.0f, 0.65f, 1.0f); // ciano
        m.position = glm::vec3(1.5f, 0.0f, 0.0f);
        m.scale    = 1.0f;
        m.VAO      = loadSimpleOBJ("../assets/Modelos3D/Cube.obj", m.nVertices, m.color);
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

    GLint modelLoc     = glGetUniformLocation(shaderID, "model");
    GLint viewLoc      = glGetUniformLocation(shaderID, "view");
    GLint projLoc      = glGetUniformLocation(shaderID, "projection");
    GLint highlightLoc = glGetUniformLocation(shaderID, "highlight");

    glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(projection));

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
            glBindVertexArray(obj.VAO);

            // Preenchimento sólido (fill levemente recuado para evitar z-fighting)
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
        }

        glfwSwapBuffers(window);
    }

    for (auto& obj : objects)
        glDeleteVertexArrays(1, &obj.VAO);
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

    // Selecionar próximo objeto
    if (key == GLFW_KEY_TAB && action == GLFW_PRESS)
    {
        selectedObj = (selectedObj + 1) % (int)objects.size();
        cout << "Objeto selecionado: " << selectedObj << endl;
        return;
    }

    OBJModel& obj = objects[selectedObj];

    // Rotação: cada eixo é um toggle; pressionar o mesmo eixo desliga
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

    // Translação X / Z (WASD)
    if (key == GLFW_KEY_W && (action == GLFW_PRESS || action == GLFW_REPEAT))
        obj.position.z -= MOVE_STEP;
    if (key == GLFW_KEY_S && (action == GLFW_PRESS || action == GLFW_REPEAT))
        obj.position.z += MOVE_STEP;
    if (key == GLFW_KEY_A && (action == GLFW_PRESS || action == GLFW_REPEAT))
        obj.position.x -= MOVE_STEP;
    if (key == GLFW_KEY_D && (action == GLFW_PRESS || action == GLFW_REPEAT))
        obj.position.x += MOVE_STEP;

    // Translação Y (setas cima/baixo)
    if (key == GLFW_KEY_UP   && (action == GLFW_PRESS || action == GLFW_REPEAT))
        obj.position.y += MOVE_STEP;
    if (key == GLFW_KEY_DOWN && (action == GLFW_PRESS || action == GLFW_REPEAT))
        obj.position.y -= MOVE_STEP;

    // Escala uniforme ([ diminui, ] aumenta)
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
// Carrega arquivo .OBJ simples e retorna o VAO
// Suporta: v, vt, vn, f (triângulos e quads)
// =========================================================
GLuint loadSimpleOBJ(const string& filePath, int& nVertices, glm::vec3 color)
{
    vector<glm::vec3> positions;
    vector<glm::vec2> texCoords;
    vector<glm::vec3> normals;
    vector<GLfloat>   vBuffer;

    ifstream file(filePath);
    if (!file.is_open())
    {
        cerr << "Erro ao abrir: " << filePath << endl;
        return 0;
    }

    string line;
    while (getline(file, line))
    {
        istringstream ss(line);
        string word;
        ss >> word;

        if (word == "v")
        {
            glm::vec3 v;
            ss >> v.x >> v.y >> v.z;
            positions.push_back(v);
        }
        else if (word == "vt")
        {
            glm::vec2 vt;
            ss >> vt.s >> vt.t;
            texCoords.push_back(vt);
        }
        else if (word == "vn")
        {
            glm::vec3 vn;
            ss >> vn.x >> vn.y >> vn.z;
            normals.push_back(vn);
        }
        else if (word == "f")
        {
            // Coleta todos os índices da face (suporta polígonos com 3+ vértices)
            vector<int> faceVerts;
            while (ss >> word)
            {
                int vi = 0, ti = 0, ni = 0;
                istringstream face(word);
                string idx;
                if (getline(face, idx, '/')) vi = idx.empty() ? 0 : stoi(idx) - 1;
                if (getline(face, idx, '/')) ti = idx.empty() ? 0 : stoi(idx) - 1;
                if (getline(face, idx))      ni = idx.empty() ? 0 : stoi(idx) - 1;
                faceVerts.push_back(vi);
            }
            // Triangulate: fan decomposition (v0, v1, v2), (v0, v2, v3), ...
            for (int k = 1; k + 1 < (int)faceVerts.size(); k++)
            {
                int tris[3] = { faceVerts[0], faceVerts[k], faceVerts[k + 1] };
                for (int vi : tris)
                {
                    vBuffer.push_back(positions[vi].x);
                    vBuffer.push_back(positions[vi].y);
                    vBuffer.push_back(positions[vi].z);
                    vBuffer.push_back(color.r);
                    vBuffer.push_back(color.g);
                    vBuffer.push_back(color.b);
                }
            }
        }
    }
    file.close();

    nVertices = (int)vBuffer.size() / 6;
    cout << "Modelo carregado: " << filePath << " (" << nVertices << " vertices)" << endl;

    GLuint VBO, VAO;
    glGenBuffers(1, &VBO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, vBuffer.size() * sizeof(GLfloat), vBuffer.data(), GL_STATIC_DRAW);

    glGenVertexArrays(1, &VAO);
    glBindVertexArray(VAO);

    // Atributo 0: posição (x, y, z)
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(GLfloat), (GLvoid*)0);
    glEnableVertexAttribArray(0);

    // Atributo 1: cor (r, g, b)
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(GLfloat), (GLvoid*)(3 * sizeof(GLfloat)));
    glEnableVertexAttribArray(1);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    return VAO;
}
