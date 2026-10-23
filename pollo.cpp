// ============================================================
//  EL POLLO CRUZA LA CALLE  -  Juego de consola en C++ (v2)
//
//  Incluye: menu de entrada animado, instrucciones, cuenta
//  regresiva, animacion de muerte (plumas, temblor, aplastado),
//  polvo al saltar, confeti al cruzar, tumba en game over,
//  festejo de victoria, pausa, record y colores.
//
//  Compilar:  g++ -std=c++17 -O2 pollo.cpp -o pollo
//  Controles: W/S o flechas = adelante/atras, A/D = lados,
//             P = pausa, Q = salir al menu
// ============================================================
#include <iostream>
#include <vector>
#include <string>
#include <random>
#include <chrono>
#include <thread>
#include <algorithm>
#include <cstdlib>
#include <cctype>

#ifdef _WIN32
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #include <windows.h>
  #include <conio.h>
#else
  #include <termios.h>
  #include <unistd.h>
  #include <sys/select.h>
#endif

// ------------------------- Entrada / terminal -------------------------
#ifdef _WIN32
static void initTerminal() {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (GetConsoleMode(h, &mode)) SetConsoleMode(h, mode | 0x0004); // ANSI
}
static void restoreTerminal() {}
static int readKey() {
    if (!_kbhit()) return 0;
    int c = _getch();
    if (c == 0 || c == 224) {              // flechas
        int c2 = _getch();
        switch (c2) { case 72: return 'w'; case 80: return 's';
                      case 75: return 'a'; case 77: return 'd'; }
        return 0;
    }
    return c;
}
#else
static termios g_old;
static void restoreTerminal() {
    std::cout << "\033[0m\033[?25h" << std::flush;
    tcsetattr(STDIN_FILENO, TCSANOW, &g_old);
}
static void initTerminal() {
    tcgetattr(STDIN_FILENO, &g_old);
    termios t = g_old;
    t.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &t);
    std::atexit(restoreTerminal);
}
static int readByte() {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    timeval tv{0, 0};
    if (select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv) > 0) {
        char c;
        if (read(STDIN_FILENO, &c, 1) == 1) return (unsigned char)c;
    }
    return 0;
}
static int readKey() {
    int c = readByte();
    if (c == 27) {                         // flechas: ESC [ A/B/C/D
        if (readByte() == '[') {
            switch (readByte()) { case 'A': return 'w'; case 'B': return 's';
                                  case 'C': return 'd'; case 'D': return 'a'; }
        }
        return 0;
    }
    return c;
}
#endif

// ------------------------- Utilidades -------------------------
const int ANCHO    = 40;
const int CARRILES = 6;
const int FILAS    = CARRILES + 2;   // fila 0 = meta, ultima = inicio
const int LARGO_AUTO = 3;
const int NIVEL_FINAL = 5;
const int TICK_MS  = 50;
static int g_record = 0;

static void dormir(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
static void vaciarTeclas() { while (readKey() != 0) {} }
static void esperarTecla() { vaciarTeclas(); while (readKey() == 0) dormir(30); }

// agrega un caracter con color ANSI (solo cambia el codigo si el color cambia)
static void celda(std::string &s, char c, int col, int &cur) {
    if (col != cur) { s += "\033[" + std::to_string(col) + "m"; cur = col; }
    s += c;
}

// ------------------------- Juego -------------------------
struct Carril {
    std::vector<bool> ocupado;
    int dir;
    int periodo;
    int contador;
};

struct Particula { float x, y, vx, vy; char ch; int color; int vida; };

struct Juego {
    std::vector<Carril> carriles;
    std::vector<Particula> part;
    int px, py;
    int vidas, nivel, puntos;
    char sprite = '@';     // aspecto del pollo (cambia en animaciones)
    int parpadeo = 0;      // parpadeo al reaparecer
    int sacudida = 0;      // temblor de pantalla (espacios a la izquierda)
    std::mt19937 rng{std::random_device{}()};

    float rf(float a, float b) { return std::uniform_real_distribution<float>(a, b)(rng); }
    int ri(int a, int b) { return std::uniform_int_distribution<int>(a, b)(rng); }

    // ----- logica -----
    void generarCarriles() {
        carriles.assign(CARRILES, Carril());
        int base = 9 - nivel;
        for (int i = 0; i < CARRILES; i++) {
            Carril &c = carriles[i];
            c.ocupado.assign(ANCHO, false);
            c.dir = (i % 2 == 0) ? 1 : -1;
            c.periodo = std::max(2, base + ri(-1, 2));
            c.contador = 0;
            int pos = ri(0, 6);
            while (pos + LARGO_AUTO < ANCHO) {
                for (int k = 0; k < LARGO_AUTO; k++) c.ocupado[pos + k] = true;
                pos += LARGO_AUTO + ri(8, 15 - (nivel > 3 ? 2 : 0));
            }
        }
    }
    void reiniciarPollo() { px = ANCHO / 2; py = FILAS - 1; sprite = '@'; parpadeo = 25; }
    void nuevoJuego() {
        vidas = 3; nivel = 1; puntos = 0; part.clear();
        generarCarriles(); reiniciarPollo(); parpadeo = 0;
    }
    bool hayAuto(int x, int y) const {
        if (y < 1 || y > CARRILES) return false;
        return carriles[y - 1].ocupado[x];
    }
    bool atropellado() const { return hayAuto(px, py); }

    void moverAutos() {
        for (Carril &c : carriles) {
            if (++c.contador < c.periodo) continue;
            c.contador = 0;
            if (c.dir > 0) {
                bool u = c.ocupado[ANCHO - 1];
                for (int x = ANCHO - 1; x > 0; x--) c.ocupado[x] = c.ocupado[x - 1];
                c.ocupado[0] = u;
            } else {
                bool p = c.ocupado[0];
                for (int x = 0; x < ANCHO - 1; x++) c.ocupado[x] = c.ocupado[x + 1];
                c.ocupado[ANCHO - 1] = p;
            }
        }
    }
    bool mover(int dx, int dy) {
        int nx = px + dx, ny = py + dy;
        if (nx < 0 || nx >= ANCHO || ny < 0 || ny >= FILAS) return false;
        part.push_back({(float)px, (float)py, 0, 0, '.', 90, 3});   // polvo del salto
        px = nx; py = ny;
        return true;
    }

    // ----- efectos -----
    void plumas() {
        const char pl[] = ".,'`~";
        for (int i = 0; i < 20; i++)
            part.push_back({(float)px, (float)py, rf(-1.3f, 1.3f), rf(-1.1f, -0.2f),
                            pl[ri(0, 4)], (i % 3 == 0) ? 91 : 97, 18});
    }
    void confeti(int n, float x, float y) {
        const char c[] = "*+o#";
        const int col[] = {91, 92, 93, 94, 95, 96};
        for (int i = 0; i < n; i++)
            part.push_back({x + rf(-1, 1), y, rf(-1.5f, 1.5f), rf(-0.4f, 0.5f),
                            c[ri(0, 3)], col[ri(0, 5)], 22});
    }
    void actualizarEfectos() {
        for (auto &p : part) { p.x += p.vx; p.y += p.vy; p.vy += 0.12f; p.vida--; }
        part.erase(std::remove_if(part.begin(), part.end(),
                   [](const Particula &p) { return p.vida <= 0; }), part.end());
        if (parpadeo > 0) parpadeo--;
    }

    // ----- dibujo -----
    void dibujar(const std::string &mensaje = "", int colMsg = 97) const {
        static const int colorCarril[CARRILES] = {91, 94, 93, 92, 95, 96};
        char ch[FILAS][ANCHO];
        int  cl[FILAS][ANCHO];
        for (int y = 0; y < FILAS; y++)
            for (int x = 0; x < ANCHO; x++) {
                ch[y][x] = ' '; cl[y][x] = 0;
                if (y == 0) { ch[y][x] = (x % 2) ? '=' : '#'; cl[y][x] = 92; }
                else if (y == FILAS - 1) { ch[y][x] = '.'; cl[y][x] = 90; }
                else if (carriles[y - 1].ocupado[x]) {
                    ch[y][x] = carriles[y - 1].dir > 0 ? '>' : '<';
                    cl[y][x] = colorCarril[y - 1];
                }
            }
        for (const auto &p : part) {
            if (p.x < 0 || p.y < 0) continue;
            int cx = (int)(p.x + 0.5f), cy = (int)(p.y + 0.5f);
            if (cx < ANCHO && cy < FILAS) { ch[cy][cx] = p.ch; cl[cy][cx] = p.color; }
        }
        if (parpadeo <= 0 || (parpadeo / 3) % 2 == 0) { ch[py][px] = sprite; cl[py][px] = 93; }

        std::string ind(sacudida, ' ');
        std::string s = "\033[H";
        s += ind + "\033[97m POLLO CRUZA LA CALLE\033[0m  Vidas: ";
        for (int i = 0; i < 3; i++) s += (i < vidas ? "\033[91m<3 \033[0m" : "   ");
        s += " Nivel " + std::to_string(nivel) + "/" + std::to_string(NIVEL_FINAL);
        s += "  Puntos: " + std::to_string(puntos) + "\033[K\n";
        s += ind + "+" + std::string(ANCHO, '-') + "+\033[K\n";
        for (int y = 0; y < FILAS; y++) {
            int cur = 0;
            s += ind + "|";
            for (int x = 0; x < ANCHO; x++) celda(s, ch[y][x], cl[y][x], cur);
            s += "\033[0m|";
            if (y == 0) s += " META";
            if (y == FILAS - 1) s += " INICIO";
            s += "\033[K\n";
        }
        s += ind + "+" + std::string(ANCHO, '-') + "+\033[K\n";
        s += "  W/S/flechas: mover   A/D: lados   P: pausa   Q: salir\033[K\n";
        s += "\033[" + std::to_string(colMsg) + "m" + mensaje + "\033[0m\033[K\n\033[J";
        std::cout << s << std::flush;
    }
    void frame(const std::string &m, int col, int ms = 60) {
        moverAutos(); actualizarEfectos(); dibujar(m, col); dormir(ms);
    }

    // ----- animaciones -----
    void animMuerte() {
        plumas();
        const char sp[] = {'@', '%', '#', 'X', 'x', '.', '.'};
        for (int f = 0; f < 21; f++) {
            sprite = sp[std::min(f / 3, 6)];
            sacudida = (f < 8) ? (f % 2 ? 2 : 0) : 0;
            frame(f % 4 < 2 ? "  *** SPLAT! ***" : "  ***  SPLAT!  ***", f % 2 ? 91 : 93);
        }
        sacudida = 0;
    }
    void animNivel() {
        for (int f = 0; f < 22; f++) {
            sprite = (f % 4 < 2) ? '^' : '@';
            if (f % 3 == 0) confeti(5, (float)px, (float)py);
            frame("  Cruzaste! +" + std::to_string(100 * nivel) + " puntos", 92);
        }
        sprite = '@';
    }
    void banner() {
        for (int f = 0; f < 12; f++)
            { moverAutos(); dibujar(f % 4 < 2 ? "  >>>>>  NIVEL " + std::to_string(nivel) + "  <<<<<" : "", 96); dormir(80); }
    }
    void animGameOver() {
        sprite = '+';
        for (int f = 0; f < 20; f++)
            frame(f % 4 < 2 ? "  G A M E   O V E R   (RIP)" : "  GAME OVER", 91, 80);
    }
    void animVictoria() {
        for (int f = 0; f < 70; f++) {
            sprite = (f % 4 < 2) ? '^' : '@';
            if (f % 4 == 0) confeti(8, (float)ri(3, ANCHO - 3), 0.0f);
            frame(f % 6 < 3 ? "  *** GANASTE! EL POLLO CRUZO TODAS LAS CALLES! ***" : "  ***   FELICIDADES!   ***", f % 2 ? 93 : 92, 60);
        }
        sprite = '@';
    }
};

// ------------------------- Partida -------------------------
static void jugar() {
    Juego g;
    g.nuevoJuego();
    std::cout << "\033[2J";
    for (int n = 3; n >= 1; n--) { g.dibujar("  Listo...  " + std::to_string(n), 93); dormir(600); }
    g.dibujar("  YA!", 92); dormir(300);
    vaciarTeclas();

    bool salir = false, terminado = false, gano = false;

    auto muerte = [&]() {
        g.vidas--;
        g.animMuerte();
        if (g.vidas <= 0) { g.animGameOver(); terminado = true; }
        else g.reiniciarPollo();
        vaciarTeclas();
    };
    auto meta = [&]() {
        g.puntos += 100 * g.nivel;
        if (g.nivel >= NIVEL_FINAL) { g.animVictoria(); terminado = true; gano = true; }
        else { g.animNivel(); g.nivel++; g.generarCarriles(); g.reiniciarPollo(); g.banner(); }
        vaciarTeclas();
    };

    while (!salir && !terminado) {
        int k;
        while ((k = readKey()) != 0) {
            k = std::tolower(k);
            if (k == 'q') { salir = true; break; }
            if (k == 'p') {
                g.dibujar("  PAUSA - pulsa P para continuar", 96);
                vaciarTeclas();
                while (true) {
                    int k2 = readKey();
                    if (k2 == 0) { dormir(30); continue; }
                    k2 = std::tolower(k2);
                    if (k2 == 'p') break;
                    if (k2 == 'q') { salir = true; break; }
                }
                if (salir) break;
                continue;
            }
            int dx = 0, dy = 0;
            if (k == 'w') dy = -1;
            else if (k == 's') dy = 1;
            else if (k == 'a') dx = -1;
            else if (k == 'd') dx = 1;
            if ((dx || dy) && g.mover(dx, dy)) {
                if (g.atropellado()) { muerte(); break; }
                if (g.py == 0)       { meta();   break; }
            }
        }
        if (salir || terminado) break;

        g.moverAutos();
        if (g.atropellado()) muerte();
        if (!terminado && g.py == 0) meta();

        g.actualizarEfectos();
        g.dibujar();
        dormir(TICK_MS);
    }

    if (!salir) {
        if (g.puntos > g_record) g_record = g.puntos;
        g.dibujar(std::string(gano ? "  VICTORIA! " : "  FIN DEL JUEGO. ") + "Puntos: " + std::to_string(g.puntos) +
                  "   -  Pulsa una tecla para volver al menu", gano ? 92 : 91);
        esperarTecla();
    }
}

// ------------------------- Menu -------------------------
static void instrucciones() {
    std::cout << "\033[2J\033[H"
        "\n  \033[93mCOMO SE JUEGA\033[0m\n\n"
        "  Eres un pollo (\033[93m@\033[0m) y debes cruzar la calle hasta la META.\n"
        "  Los autos (\033[91m>>>\033[0m y \033[94m<<<\033[0m) te atropellan si los tocas.\n\n"
        "   W / flecha arriba ..... avanzar\n"
        "   S / flecha abajo ...... retroceder\n"
        "   A / D ................. esquivar a los lados\n"
        "   P ..................... pausa\n"
        "   Q ..................... salir al menu\n\n"
        "  Tienes 3 vidas. Hay 5 niveles y en cada uno los autos\n"
        "  van mas rapido. Cruza el nivel 5 para ganar.\n\n"
        "  \033[90mPulsa cualquier tecla para volver...\033[0m\n" << std::flush;
    esperarTecla();
}

static int menu() {
    int sel = 0, f = 0;
    const char *ops[3] = {"JUGAR", "COMO SE JUEGA", "SALIR"};
    const char *titulo[5] = {
        R"( ____    ___   _      _      ___  )",
        R"(|  _ \  / _ \ | |    | |    / _ \ )",
        R"(| |_) || | | || |    | |   | | | |)",
        R"(|  __/ | |_| || |___ | |___| |_| |)",
        R"(|_|     \___/ |_____||_____|\___/ )"};
    std::cout << "\033[2J";
    while (true) {
        int k;
        while ((k = readKey()) != 0) {
            k = std::tolower(k);
            if (k == 'w' && sel > 0) sel--;
            if (k == 's' && sel < 2) sel++;
            if (k == 13 || k == 10 || k == ' ') return sel;
            if (k == 'q') return 2;
        }
        std::string s = "\033[H\n";
        for (int i = 0; i < 5; i++) s += std::string("\033[93m   ") + titulo[i] + "\033[0m\033[K\n";
        s += "\n\033[96m         C R U Z A   L A   C A L L E\033[0m\033[K\n\n";
        for (int i = 0; i < 3; i++) {
            if (i == sel) s += std::string((f / 4) % 2 ? "\033[92m" : "\033[1;92m") + "         >>  " + ops[i] + "  <<";
            else          s += std::string("\033[37m            ") + ops[i];
            s += "\033[0m\033[K\n";
        }
        s += "\n\033[90m   W/S: mover    ENTER: elegir    Q: salir\033[0m\033[K\n";
        s += "\033[93m   Record: " + std::to_string(g_record) + "\033[0m\033[K\n\n";

        // animacion: un pollo pasea por la acera y un auto pasa por la calle
        char acera[ANCHO + 1], calle[ANCHO + 1];
        for (int x = 0; x < ANCHO; x++) { acera[x] = '.'; calle[x] = ' '; }
        acera[ANCHO] = calle[ANCHO] = '\0';
        int ciclo = (ANCHO - 1) * 2, t = (f / 3) % ciclo;
        int chX = t < ANCHO ? t : ciclo - t;
        int carX = (f / 2) % (ANCHO + 8) - 4;
        int cur = 0;
        std::string r1, r2;
        for (int x = 0; x < ANCHO; x++) {
            if (x == chX) celda(r1, (f / 3) % 2 ? '@' : 'o', 93, cur); else celda(r1, acera[x], 90, cur);
        }
        r1 += "\033[0m"; cur = 0;
        const char car[] = "[##>";
        for (int x = 0; x < ANCHO; x++) {
            int i = x - carX;
            if (i >= 0 && i < 4) celda(r2, car[i], 91, cur); else celda(r2, ' ', 0, cur);
        }
        r2 += "\033[0m";
        s += "   " + r1 + "\033[K\n   " + r2 + "\033[K\n\033[J";
        std::cout << s << std::flush;
        f++;
        dormir(60);
    }
}

int main() {
    initTerminal();
    std::cout << "\033[2J\033[?25l";
    while (true) {
        int op = menu();
        if (op == 2) break;
        std::cout << "\033[2J";
        if (op == 1) instrucciones(); else jugar();
        std::cout << "\033[2J";
    }
    std::cout << "\033[2J\033[H\033[0m\033[?25h\n  Gracias por jugar!\n\n";
    restoreTerminal();
    return 0;
}
