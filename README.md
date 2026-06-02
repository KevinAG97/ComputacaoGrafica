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
│   │  ├── 🟢DesafioM1.jpeg             # Print Desafio do módulo 1
│   │  ├── 🔵DesafioM2.png              # Print Desafio do módulo 2
│   │  ├── 🔴Vivencial2.png             # Print da aula Vivencial do dia 09/05/2026   
│   │  ├── 🟡DesafioM3.png              # Print Desafio do módulo 3
│   │  ├── 🟣Vivencial23-05-2026.png    # Print da aula Vivencial do dia 26/05/2026
│   │  ├── ⚪DesafioM4.png              # Print Desafio do módulo 4
│   │  ├── 🟠DesafioM5.png              # Print Desafio do módulo 5
│   │
│   │       
├── 📂 src/                   # Código-fonte dos exemplos e exercícios
│   ├── Hello3D.cpp           # 🟢Módulo 1
│   ├── Cubos3D.cpp           # 🔵Módulo 2
│   ├── Vivencial2.cpp        # 🔴Vivencial 09/05/2026
│   ├── Modulo3.cpp           # 🟡Módulo 3
│   ├── VivencialM4.cpp       # 🟣Vivencial 26/05/2026
│   ├── IluminacaoM4.cpp      # 🟠Módulo 4
│   ├── DesafioM5.cpp         # 🟢Módulo 5
│
├── 📄 CMakeLists.txt         # Configuração do CMake para compilar os projetos
├── 📄 README.md              # Este arquivo, com a documentação do repositório 


