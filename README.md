# Computação Gráfica - Híbrido
# ⚠️ATENÇÂO⚠️ ver abaixo em "Estrutura do Repositório" onde se localizam os arquivos de desafios de cada módulo

## 🤖 Aluno

```glsl
// student.vert
uniform struct Student {
    string name  = "Kevin de Azevedo Garcia";
    int    ra    = 1937232;
    string curso = "Ciência da Computação";
    string modal = "Híbrido";
    string inst  = "Unisinos";
} aluno;
```

Repositório de códigos em C++ utilizando OpenGL moderna (3.3+) criado para a Atividade Acadêmica Computação Gráfica do curso de graduação em Ciência da Computação - modalidade híbrida - da Unisinos.

## 📂 Estrutura do Repositório

```plaintext
📂 COMPUTACAOGRAFICA/
├── 📂 assets/                # Guardo prints com o nome do desafio de cada módulo, texturas, fontes etc
│   ├──                           
│   │  ├── 🟢DesafioM1.jpeg     # Print Desafio do módulo 1
│   │  ├── 🔵DesafioM2.jpeg     # Print Desafio do módulo 2
│   │  ├──                    
│   │       
├── 📂 src/                   # Código-fonte dos exemplos e exercícios
│   ├── Hello3D.cpp           # 🟢Módulo 1
│   ├── Cubos3D.cpp           # 🔵Módulo 2
├── 📄 CMakeLists.txt         # Configuração do CMake para compilar os projetos
├── 📄 README.md              # Este arquivo, com a documentação do repositório