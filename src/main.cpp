#include <raylib.h>
#include <rlgl.h>
#include <raymath.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <string>
#include <vector>
#include <unistd.h>

int columns = 54;
int rows = 30;
constexpr int modelCount = 8;

const char* names[] = {"VIAJERO", "CAPUCHA DE MALLA", "ANTORCHA", "GATO", "ESPADA", "RATA", "NO-MUERTO", "ESCUDO"};
const char* descriptions[] = {"Un viajero de tierras olvidadas.", "Proteccion de hierro entrelazado.",
                              "Una llama contra la oscuridad.", "Un pequeno habitante de las ruinas.",
                              "Hoja de acero de un viajero.", "Criatura de las profundidades.",
                              "Un cuerpo que se niega a descansar.", "Una defensa contra la noche."};
// Orden solicitado: de mayor a menor intensidad luminosa.
const char* symbols[] = {"@", "¶", "$", "&", "}", "?", "/", "*", "!", "~", ";", "."};
const Color palette[] = {{235, 235, 230, 255}, {172, 194, 218, 255}, {202, 140, 105, 255},
                         {170, 190, 145, 255}, {205, 180, 120, 255}};

struct Part
{
    int type;
    Vector3 position;
    Vector3 size;
    int colorIndex;
    Vector3 rotation{};
    bool emissive = false;
    Color color = {235, 235, 230, 255};
    float bend = 0.0f;
    float thickness = 0.04f;
    bool mirrorX = false, mirrorY = false, mirrorZ = false;
    bool lightHidden = false;
    float lightStrength = 1.0f;
    bool rigid = false;
    bool physicsFixed = false;
    float bounce = 0.25f;
    float softness = 0.0f;
    float hardness = 1.0f;
    float mass = 1.0f;
    float airDrag = 0.0f;
    float friction = 0.4f;
};

bool SameParts(const std::vector<Part>& a, const std::vector<Part>& b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
    {
        const Part& x = a[i]; const Part& y = b[i];
        if (x.type != y.type || x.colorIndex != y.colorIndex || x.emissive != y.emissive ||
            x.position.x != y.position.x || x.position.y != y.position.y || x.position.z != y.position.z ||
            x.size.x != y.size.x || x.size.y != y.size.y || x.size.z != y.size.z ||
            x.rotation.x != y.rotation.x || x.rotation.y != y.rotation.y || x.rotation.z != y.rotation.z ||
            x.color.r != y.color.r || x.color.g != y.color.g || x.color.b != y.color.b ||
            x.bend != y.bend || x.thickness != y.thickness || x.mirrorX != y.mirrorX || x.mirrorY != y.mirrorY || x.mirrorZ != y.mirrorZ ||
            x.lightHidden != y.lightHidden || x.lightStrength != y.lightStrength || x.rigid != y.rigid ||
            x.physicsFixed != y.physicsFixed || x.bounce != y.bounce || x.softness != y.softness ||
            x.hardness != y.hardness || x.mass != y.mass || x.airDrag != y.airDrag || x.friction != y.friction) return false;
    }
    return true;
}

struct EditorState { std::vector<Part> parts; std::vector<int> selection; int active; };

void SnapCameraToNearestSide(float& yaw, float& pitch)
{
    Vector3 direction = {cosf(pitch) * sinf(yaw), sinf(pitch), cosf(pitch) * cosf(yaw)};
    float x = fabsf(direction.x), y = fabsf(direction.y), z = fabsf(direction.z);
    if (y >= x && y >= z)
    {
        pitch = direction.y >= 0 ? PI / 2 : -PI / 2;
        yaw = 0;
    }
    else if (x >= z)
    {
        yaw = direction.x >= 0 ? PI / 2 : -PI / 2;
        pitch = 0;
    }
    else
    {
        yaw = direction.z >= 0 ? 0 : PI;
        pitch = 0;
    }
}

void DrawPixelButton(Rectangle rect, Color fill, bool disabled = false)
{
    Color edge = disabled ? Color{70, 71, 68, 255} : Color{145, 146, 126, 255};
    Color shadow = {10, 12, 13, 255};
    if (disabled) fill = {38, 39, 38, 255};
    DrawRectangle(rect.x + 3, rect.y + 3, rect.width, rect.height, shadow);
    DrawRectangleRec(rect, fill);
    DrawRectangleLinesEx(rect, 1, edge);
    DrawRectangle(rect.x + 3, rect.y + 3, rect.width - 6, 2, Fade(edge, disabled ? 0.15f : 0.4f));
    DrawRectangle(rect.x + 3, rect.y + rect.height - 4, rect.width - 6, 2, {18, 20, 20, 180});
}

enum class Tool { Select = -1, Move = 0, Rotate, Scale, Extrude };
const char* toolNames[] = {"MOVER", "GIRAR", "ESCALA", "EXTRUIR"};

std::filesystem::path ModelDirectory()
{
    const char* home = std::getenv("HOME");
    return (home ? std::filesystem::path(home) : std::filesystem::path(".")) / ".local/share/asciiforge/models";
}

bool SaveModel(const std::vector<Part>& parts, std::string& filename, std::string& message, int exampleSource)
{
    namespace fs = std::filesystem;
    std::error_code error;
    fs::create_directories(ModelDirectory(), error);
    if (error) { message = "No se pudo crear la carpeta de modelos"; return false; }
    fs::path path = filename.empty() ? fs::path{} : ModelDirectory() / fs::path(filename).filename();
    if (!filename.empty() && !fs::is_regular_file(path, error))
    { message = "El archivo original ya no existe o no se puede abrir"; return false; }
    if (filename.empty()) for (int i = 1; i <= 9999; ++i)
    {
        path = ModelDirectory() / ("modelo_" + std::to_string(i) + ".afm");
        bool exists = fs::exists(path, error);
        if (error) { message = "No se pudo consultar la carpeta de modelos"; return false; }
        if (!exists) break;
        path.clear();
    }
    if (path.empty()) { message = "No quedan nombres disponibles"; return false; }
    std::string tempTemplate = (ModelDirectory() / ".asciiforge-XXXXXX").string();
    std::vector<char> tempName(tempTemplate.begin(), tempTemplate.end());
    tempName.push_back('\0');
    int fd = mkstemp(tempName.data());
    if (fd < 0) { message = "No se pudo crear un archivo temporal"; return false; }
    close(fd);
    fs::path tempPath(tempName.data());
    std::ofstream file(tempPath);
    file << "ASCIIFORGE 11\n" << parts.size() << ' ' << exampleSource << '\n';
    for (const Part& p : parts)
        file << p.type << ' ' << p.colorIndex << ' ' << p.position.x << ' ' << p.position.y << ' ' << p.position.z << ' '
             << p.size.x << ' ' << p.size.y << ' ' << p.size.z << ' ' << p.rotation.x << ' ' << p.rotation.y << ' ' << p.rotation.z << ' ' << p.emissive << ' '
             << static_cast<int>(p.color.r) << ' ' << static_cast<int>(p.color.g) << ' ' << static_cast<int>(p.color.b) << ' ' << p.bend << ' ' << p.thickness << ' '
             << p.mirrorX << ' ' << p.mirrorY << ' ' << p.mirrorZ << ' ' << p.lightHidden << ' ' << p.lightStrength << ' ' << p.rigid << ' '
             << p.physicsFixed << ' ' << p.bounce << ' ' << p.softness << ' ' << p.hardness << ' ' << p.mass << ' ' << p.airDrag << ' ' << p.friction << '\n';
    file.close();
    if (!file) { fs::remove(tempPath, error); message = "Error al guardar el modelo"; return false; }
    fs::rename(tempPath, path, error);
    if (error) { std::error_code ignored; fs::remove(tempPath, ignored); message = "Error al actualizar el archivo"; return false; }
    filename = path.filename().string();
    message = "Guardado: " + path.filename().string();
    return true;
}

std::vector<std::filesystem::path> ListModels(std::error_code& error)
{
    namespace fs = std::filesystem;
    std::vector<fs::path> files;
    fs::directory_iterator it(ModelDirectory(), error), end;
    if (error) return files;
    for (; it != end; it.increment(error))
    {
        if (error) break;
        if (it->path().extension() == ".afm") files.push_back(it->path());
    }
    std::sort(files.begin(), files.end());
    return files;
}

bool LoadNextModel(std::vector<Part>& parts, std::string& lastFile, std::string& message, const std::string& requested = "", int* exampleSource = nullptr)
{
    namespace fs = std::filesystem;
    std::error_code error;
    std::vector<fs::path> files = ListModels(error);
    if (error) { message = "Error al listar modelos"; return false; }
    if (files.empty()) { message = "No hay modelos guardados"; return false; }
    std::sort(files.begin(), files.end());
    size_t index = 0;
    if (!requested.empty())
    {
        bool found = false;
        for (size_t i = 0; i < files.size(); ++i)
            if (files[i].filename().string() == requested) { index = i; found = true; break; }
        if (!found) { message = "El modelo ya no existe"; return false; }
    }
    else for (size_t i = 0; i < files.size(); ++i)
        if (files[i].filename().string() == lastFile) { index = (i + 1) % files.size(); break; }
    std::ifstream file(files[index]);
    std::string header;
    int version = 0;
    size_t count = 0;
    if (!(file >> header >> version >> count) || header != "ASCIIFORGE" || version < 1 || version > 11 || count > 10000)
    { message = "Archivo de modelo invalido"; return false; }
    int source = -1;
    if (version >= 5 && (!(file >> source) || source < -1 || source >= modelCount))
    { message = "Archivo de modelo invalido"; return false; }
    std::vector<Part> loaded;
    for (size_t i = 0; i < count; ++i)
    {
        Part p{};
        if (!(file >> p.type >> p.colorIndex >> p.position.x >> p.position.y >> p.position.z >> p.size.x >> p.size.y >> p.size.z >> p.rotation.x >> p.rotation.y >> p.rotation.z) ||
            p.type < 0 || p.type > (version >= 6 ? 13 : version >= 4 ? 12 : 4) || p.colorIndex < 0 || p.colorIndex >= 5 ||
            !std::isfinite(p.position.x) || !std::isfinite(p.position.y) || !std::isfinite(p.position.z) ||
            !std::isfinite(p.rotation.x) || !std::isfinite(p.rotation.y) || !std::isfinite(p.rotation.z) ||
            !(p.size.x >= 0.04f && p.size.x <= 20 && p.size.y >= 0.04f && p.size.y <= 20 && p.size.z >= 0.04f && p.size.z <= 20))
        { message = "Archivo de modelo invalido"; return false; }
        int emission = 0;
        if (version >= 2 && (!(file >> emission) || (emission != 0 && emission != 1)))
        { message = "Archivo de modelo invalido"; return false; }
        p.emissive = emission != 0;
        p.color = palette[p.colorIndex];
        if (version >= 3)
        {
            int r, g, b;
            if (!(file >> r >> g >> b) || r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255)
            { message = "Archivo de modelo invalido"; return false; }
            p.color = {static_cast<unsigned char>(r), static_cast<unsigned char>(g), static_cast<unsigned char>(b), 255};
        }
        if (version >= 6 && (!(file >> p.bend >> p.thickness) || !std::isfinite(p.bend) || !std::isfinite(p.thickness) || p.bend < -360 || p.bend > 360 || p.thickness < 0.001f || p.thickness > 0.5f))
        { message = "Archivo de modelo invalido"; return false; }
        if (version >= 7)
        {
            int x, y, z;
            if (!(file >> x >> y >> z) || x < 0 || x > 1 || y < 0 || y > 1 || z < 0 || z > 1)
            { message = "Archivo de modelo invalido"; return false; }
            p.mirrorX = x != 0; p.mirrorY = y != 0; p.mirrorZ = z != 0;
        }
        if (version >= 8)
        {
            int hidden;
            if (!(file >> hidden >> p.lightStrength) || (hidden != 0 && hidden != 1) || !std::isfinite(p.lightStrength) || p.lightStrength < 0 || p.lightStrength > 3 || (hidden && !p.emissive))
            { message = "Archivo de modelo invalido"; return false; }
            p.lightHidden = hidden != 0;
        }
        if (version >= 9)
        {
            int rigid;
            if (!(file >> rigid) || (rigid != 0 && rigid != 1)) { message = "Archivo de modelo invalido"; return false; }
            p.rigid = rigid != 0;
        }
        if (version >= 10)
        {
            int fixed;
            if (!(file >> fixed >> p.bounce >> p.softness) || (fixed != 0 && fixed != 1) ||
                !std::isfinite(p.bounce) || p.bounce < 0 || p.bounce > (version >= 11 ? 1.5f : 1.0f) ||
                !std::isfinite(p.softness) || p.softness < 0 || p.softness > 1)
            { message = "Archivo de modelo invalido"; return false; }
            p.physicsFixed = fixed != 0;
        }
        p.hardness = 1.0f - p.softness;
        if (version >= 11 && (!(file >> p.hardness >> p.mass >> p.airDrag >> p.friction) ||
            !std::isfinite(p.hardness) || p.hardness < 0 || p.hardness > 2 ||
            !std::isfinite(p.mass) || p.mass < 0.001f || p.mass > 1000 ||
            !std::isfinite(p.airDrag) || p.airDrag < 0 || p.airDrag > 2 ||
            !std::isfinite(p.friction) || p.friction < 0 || p.friction > 1))
        { message = "Archivo de modelo invalido"; return false; }
        loaded.push_back(p);
    }
    parts = std::move(loaded);
    if (version < 5 && !parts.empty() && parts.front().type >= 5 && parts.front().type < 5 + modelCount)
        source = parts.front().type - 5;
    if (exampleSource) *exampleSource = source;
    lastFile = files[index].filename().string();
    message = "Abierto: " + lastFile;
    return true;
}

const char* partNames[] = {"CUBO", "ESFERA", "CONO", "CILINDRO", "PLANO"};
void DrawModel(int selected);

// Faldon abierto por debajo, con superficie interior y borde de espesor visible.
void DrawHoodSkirt()
{
    const int segments = 32;
    rlBegin(RL_QUADS);
    for (int i = 0; i < segments; ++i)
    {
        float a = 2 * PI * i / segments, b = 2 * PI * (i + 1) / segments;
        float ax = sinf(a), az = cosf(a), bx = sinf(b), bz = cosf(b);
        rlColor4ub(175, 175, 175, 255);
        rlNormal3f(ax, 0.3f, az);
        rlVertex3f(ax * 0.92f, 0.82f, az * 0.92f);
        rlVertex3f(ax * 0.52f, 1.67f, az * 0.52f);
        rlVertex3f(bx * 0.52f, 1.67f, bz * 0.52f);
        rlVertex3f(bx * 0.92f, 0.82f, bz * 0.92f);
        rlColor4ub(85, 85, 85, 255);
        rlNormal3f(-ax, -0.3f, -az);
        rlVertex3f(bx * 0.85f, 0.82f, bz * 0.85f);
        rlVertex3f(bx * 0.45f, 1.67f, bz * 0.45f);
        rlVertex3f(ax * 0.45f, 1.67f, az * 0.45f);
        rlVertex3f(ax * 0.85f, 0.82f, az * 0.85f);
        rlNormal3f(0, -1, 0);
        rlVertex3f(ax * 0.92f, 0.82f, az * 0.92f);
        rlVertex3f(bx * 0.92f, 0.82f, bz * 0.92f);
        rlVertex3f(bx * 0.85f, 0.82f, bz * 0.85f);
        rlVertex3f(ax * 0.85f, 0.82f, az * 0.85f);
    }
    rlEnd();
}

void SurfaceQuad(Vector3 a, Vector3 b, Vector3 c, Vector3 d)
{
    Vector3 normal = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(c, a)));
    rlNormal3f(normal.x, normal.y, normal.z);
    for (Vector3 v : {a, b, c, d}) rlVertex3f(v.x, v.y, v.z);
}

Vector3 BentPoint(float x, float z, float offset, float bend, float aspect = 1.0f)
{
    float k = bend * DEG2RAD;
    if (fabsf(k) < 0.0001f) return {x, offset, z};
    float angle = k * x;
    return {sinf(angle) / k - sinf(angle) * offset / aspect, (1 - cosf(angle)) / k * aspect + cosf(angle) * offset, z};
}

Matrix PartRotation(const Part& p)
{
    return MatrixMultiply(MatrixRotateZ(p.rotation.z * DEG2RAD), MatrixMultiply(MatrixRotateX(p.rotation.x * DEG2RAD), MatrixRotateY(p.rotation.y * DEG2RAD)));
}

Matrix PartInverseTransform(const Part& p)
{
    Matrix scale = MatrixScale(p.size.x * (p.mirrorX ? -1.0f : 1.0f),
                               p.size.y * (p.mirrorY ? -1.0f : 1.0f),
                               p.size.z * (p.mirrorZ ? -1.0f : 1.0f));
    Matrix transform = MatrixMultiply(MatrixMultiply(scale, PartRotation(p)), MatrixTranslate(p.position.x, p.position.y, p.position.z));
    return MatrixInvert(transform);
}

BoundingBox PartBounds(const Part& p)
{
    BoundingBox result{{1e9f, 1e9f, 1e9f}, {-1e9f, -1e9f, -1e9f}};
    Matrix rotation = PartRotation(p);
    auto include = [&](Vector3 point) {
        point.x *= p.size.x * (p.mirrorX ? -1 : 1);
        point.y *= p.size.y * (p.mirrorY ? -1 : 1);
        point.z *= p.size.z * (p.mirrorZ ? -1 : 1);
        point = Vector3Add(p.position, Vector3Transform(point, rotation));
        result.min = Vector3Min(result.min, point);
        result.max = Vector3Max(result.max, point);
    };
    if (p.type == 4)
    {
        for (int i = 0; i <= 32; ++i)
            for (int y : {-1, 1}) for (int z : {-1, 1})
                include(BentPoint(-0.5f + i / 32.0f, z * 0.5f, y * p.thickness * 0.5f, p.bend, p.size.x / p.size.y));
    }
    else
    {
        Vector3 half = p.type == 13 ? Vector3{0.5f, 0.15f, 0.5f} :
                       p.type >= 5 ? Vector3{1.1f, 1.25f, 1.1f} : Vector3{0.5f, 0.5f, 0.5f};
        for (int x : {-1, 1}) for (int y : {-1, 1}) for (int z : {-1, 1})
            include({x * half.x, y * half.y, z * half.z});
    }
    return result;
}

float OverlapVolume(BoundingBox a, BoundingBox b)
{
    float x = std::max(0.0f, std::min(a.max.x, b.max.x) - std::max(a.min.x, b.min.x) - 0.0001f);
    float y = std::max(0.0f, std::min(a.max.y, b.max.y) - std::max(a.min.y, b.min.y) - 0.0001f);
    float z = std::max(0.0f, std::min(a.max.z, b.max.z) - std::max(a.min.z, b.min.z) - 0.0001f);
    return x * y * z;
}

bool UniformScale(const Part& p)
{
    return fabsf(p.size.x - p.size.y) < 0.0001f && fabsf(p.size.x - p.size.z) < 0.0001f;
}

// Prueba precisa para una esfera uniforme frente a otra esfera o una
// primitiva redonda uniforme. Las demas combinaciones conservan su caja.
bool ShapesOverlap(const Part& a, const Part& b)
{
    const Part* sphere = &a;
    const Part* other = &b;
    if (sphere->type != 1 || !UniformScale(*sphere)) std::swap(sphere, other);
    if (sphere->type != 1 || !UniformScale(*sphere)) return true;
    float radius = sphere->size.x * 0.5f;
    if (other->type == 1 && UniformScale(*other))
        return Vector3Distance(sphere->position, other->position) < radius + other->size.x * 0.5f - 0.0001f;
    if ((other->type != 2 && other->type != 3 && other->type != 13) || !UniformScale(*other)) return true;
    Vector3 point = Vector3Transform(sphere->position, PartInverseTransform(*other));
    float radial = sqrtf(point.x * point.x + point.z * point.z);
    float distance = 0;
    if (other->type == 3)
    {
        float x = radial - 0.5f, y = fabsf(point.y) - 0.5f;
        distance = std::min(std::max(x, y), 0.0f) + sqrtf(std::max(x, 0.0f) * std::max(x, 0.0f) + std::max(y, 0.0f) * std::max(y, 0.0f));
    }
    else if (other->type == 13)
        distance = sqrtf((radial - 0.35f) * (radial - 0.35f) + point.y * point.y) - 0.15f;
    else
    {
        Vector2 q{radial, point.y}, k1{0, 0.5f}, k2{-0.5f, 1.0f};
        Vector2 ca{q.x - std::min(q.x, q.y < 0 ? 0.5f : 0.0f), fabsf(q.y) - 0.5f};
        float t = std::clamp(Vector2DotProduct(Vector2Subtract(k1, q), k2) / Vector2DotProduct(k2, k2), 0.0f, 1.0f);
        Vector2 cb = Vector2Add(Vector2Subtract(q, k1), Vector2Scale(k2, t));
        float sign = cb.x < 0 && ca.y < 0 ? -1.0f : 1.0f;
        distance = sign * sqrtf(std::min(Vector2DotProduct(ca, ca), Vector2DotProduct(cb, cb)));
    }
    return distance * other->size.x < radius - 0.0001f;
}

bool CrossedBoxes(BoundingBox a0, BoundingBox a1, BoundingBox b0, BoundingBox b1)
{
    Vector3 ca0 = Vector3Scale(Vector3Add(a0.min, a0.max), 0.5f), cb0 = Vector3Scale(Vector3Add(b0.min, b0.max), 0.5f);
    Vector3 ca1 = Vector3Scale(Vector3Add(a1.min, a1.max), 0.5f), cb1 = Vector3Scale(Vector3Add(b1.min, b1.max), 0.5f);
    Vector3 origin = Vector3Subtract(ca0, cb0), direction = Vector3Subtract(Vector3Subtract(ca1, ca0), Vector3Subtract(cb1, cb0));
    Vector3 ea = Vector3Scale(Vector3Max(Vector3Subtract(a0.max, a0.min), Vector3Subtract(a1.max, a1.min)), 0.5f);
    Vector3 eb = Vector3Scale(Vector3Max(Vector3Subtract(b0.max, b0.min), Vector3Subtract(b1.max, b1.min)), 0.5f);
    float os[] = {origin.x, origin.y, origin.z}, ds[] = {direction.x, direction.y, direction.z};
    float limits[] = {ea.x + eb.x - 0.0001f, ea.y + eb.y - 0.0001f, ea.z + eb.z - 0.0001f};
    float enter = 0, exit = 1;
    for (int axis = 0; axis < 3; ++axis)
    {
        if (fabsf(ds[axis]) < 1e-8f) { if (fabsf(os[axis]) >= limits[axis]) return false; }
        else
        {
            float a = (-limits[axis] - os[axis]) / ds[axis], b = (limits[axis] - os[axis]) / ds[axis];
            if (a > b) std::swap(a, b);
            enter = std::max(enter, a); exit = std::min(exit, b);
            if (enter >= exit) return false;
        }
    }
    return enter < 1 && exit > 0;
}

bool HasNewCollision(const std::vector<Part>& before, const std::vector<Part>& after)
{
    if (after.size() < before.size()) return false;
    for (size_t i = 0; i < after.size(); ++i) for (size_t j = i + 1; j < after.size(); ++j)
    {
        if (!after[i].rigid && !after[j].rigid) continue;
        BoundingBox a = PartBounds(after[i]), b = PartBounds(after[j]);
        float overlap = ShapesOverlap(after[i], after[j]) ? OverlapVolume(a, b) : 0;
        bool existed = i < before.size() && j < before.size();
        float previousBox = existed ? OverlapVolume(PartBounds(before[i]), PartBounds(before[j])) : 0;
        float previous = existed && ShapesOverlap(before[i], before[j]) ? previousBox : 0;
        if (overlap > previous + 1e-8f) return true;
        if (existed && previous > 0 && overlap >= previous - 1e-8f)
        {
            float oldDistance = Vector3DistanceSqr(before[i].position, before[j].position);
            float newDistance = Vector3DistanceSqr(after[i].position, after[j].position);
            if (newDistance + 1e-8f < oldDistance) return true;
        }
        if (existed && previousBox == 0 && previous == 0 && overlap == 0 && CrossedBoxes(PartBounds(before[i]), a, PartBounds(before[j]), b)) return true;
    }
    return false;
}

// Vista previa sin alterar el modelo del taller. Se utilizan cajas envolventes,
// masas variables y subpasos; la dureza ajusta el contacto, no deforma la malla.
void StepPhysics(std::vector<Part>& bodies, std::vector<Vector3>& velocity, float frameTime)
{
    if (velocity.size() != bodies.size()) velocity.assign(bodies.size(), {});
    float dt = std::clamp(frameTime, 0.0f, 0.05f);
    int steps = std::max(1, static_cast<int>(ceilf(dt * 240.0f)));
    float step = dt / steps;
    for (int s = 0; s < steps; ++s)
    {
        std::vector<BoundingBox> previous;
        previous.reserve(bodies.size());
        for (const Part& p : bodies) previous.push_back(PartBounds(p));
        for (size_t i = 0; i < bodies.size(); ++i)
        {
            if (!bodies[i].rigid || bodies[i].physicsFixed) continue;
            velocity[i].y -= 9.8f * step;
            // Resistencia cuadratica simplificada: F = -b * |v| * v.
            float speed = Vector3Length(velocity[i]);
            float dragFactor = 1.0f / (1.0f + bodies[i].airDrag * speed * step / bodies[i].mass);
            velocity[i] = Vector3Scale(velocity[i], dragFactor);
            bodies[i].position = Vector3Add(bodies[i].position, Vector3Scale(velocity[i], step));
        }
        for (size_t i = 0; i < bodies.size(); ++i) for (size_t j = i + 1; j < bodies.size(); ++j)
        {
            if (!bodies[i].rigid || !bodies[j].rigid || (bodies[i].physicsFixed && bodies[j].physicsFixed)) continue;
            BoundingBox a = PartBounds(bodies[i]), b = PartBounds(bodies[j]);
            float overlaps[] = {std::min(a.max.x, b.max.x) - std::max(a.min.x, b.min.x),
                                std::min(a.max.y, b.max.y) - std::max(a.min.y, b.min.y),
                                std::min(a.max.z, b.max.z) - std::max(a.min.z, b.min.z)};
            bool inside = overlaps[0] > 0 && overlaps[1] > 0 && overlaps[2] > 0;
            if (inside && !ShapesOverlap(bodies[i], bodies[j])) continue;
            bool crossed = !inside && CrossedBoxes(previous[i], a, previous[j], b);
            if (!inside && !crossed) continue;
            Vector3 ca = Vector3Scale(Vector3Add(a.min, a.max), 0.5f), cb = Vector3Scale(Vector3Add(b.min, b.max), 0.5f);
            int axis = 0;
            if (inside) { if (overlaps[1] < overlaps[axis]) axis = 1; if (overlaps[2] < overlaps[axis]) axis = 2; }
            else
            {
                Vector3 ac = Vector3Scale(Vector3Add(previous[i].min, previous[i].max), 0.5f);
                Vector3 bc = Vector3Scale(Vector3Add(previous[j].min, previous[j].max), 0.5f);
                Vector3 ah = Vector3Scale(Vector3Subtract(previous[i].max, previous[i].min), 0.5f);
                Vector3 bh = Vector3Scale(Vector3Subtract(previous[j].max, previous[j].min), 0.5f);
                float gaps[] = {fabsf(bc.x-ac.x)-ah.x-bh.x, fabsf(bc.y-ac.y)-ah.y-bh.y, fabsf(bc.z-ac.z)-ah.z-bh.z};
                if (gaps[1] > gaps[axis]) axis = 1; if (gaps[2] > gaps[axis]) axis = 2;
                ca = ac; cb = bc;
                if (!bodies[i].physicsFixed) bodies[i].position = Vector3Subtract(bodies[i].position, Vector3Scale(velocity[i], step));
                if (!bodies[j].physicsFixed) bodies[j].position = Vector3Subtract(bodies[j].position, Vector3Scale(velocity[j], step));
            }
            float sign = axis == 0 ? (cb.x >= ca.x ? 1.0f : -1.0f) : axis == 1 ? (cb.y >= ca.y ? 1.0f : -1.0f) : (cb.z >= ca.z ? 1.0f : -1.0f);
            Vector3 normal = axis == 0 ? Vector3{sign,0,0} : axis == 1 ? Vector3{0,sign,0} : Vector3{0,0,sign};
            float wi = bodies[i].physicsFixed ? 0.0f : 1.0f / bodies[i].mass, wj = bodies[j].physicsFixed ? 0.0f : 1.0f / bodies[j].mass;
            float hardness = (bodies[i].hardness + bodies[j].hardness) * 0.5f;
            if (inside)
            {
                float correction = (overlaps[axis] + 0.0001f) * (0.2f + 0.4f * hardness) / (wi + wj);
                bodies[i].position = Vector3Subtract(bodies[i].position, Vector3Scale(normal, correction * wi));
                bodies[j].position = Vector3Add(bodies[j].position, Vector3Scale(normal, correction * wj));
            }
            float relative = Vector3DotProduct(Vector3Subtract(velocity[j], velocity[i]), normal);
            if (relative < 0)
            {
                float bounce = std::max(bodies[i].bounce, bodies[j].bounce);
                // En impactos muy lentos se elimina el rebote residual para permitir reposo.
                if (-relative < 0.2f) bounce = 0.0f;
                float impulse = -(1.0f + bounce) * relative / (wi + wj);
                velocity[i] = Vector3Subtract(velocity[i], Vector3Scale(normal, impulse * wi));
                velocity[j] = Vector3Add(velocity[j], Vector3Scale(normal, impulse * wj));
                Vector3 relativeVelocity = Vector3Subtract(velocity[j], velocity[i]);
                Vector3 tangent = Vector3Subtract(relativeVelocity, Vector3Scale(normal, Vector3DotProduct(relativeVelocity, normal)));
                float tangentLength = Vector3Length(tangent);
                if (tangentLength > 0.0001f)
                {
                    tangent = Vector3Scale(tangent, 1.0f / tangentLength);
                    float friction = sqrtf(bodies[i].friction * bodies[j].friction);
                    float frictionImpulse = std::min(tangentLength / (wi + wj), friction * impulse);
                    velocity[i] = Vector3Add(velocity[i], Vector3Scale(tangent, frictionImpulse * wi));
                    velocity[j] = Vector3Subtract(velocity[j], Vector3Scale(tangent, frictionImpulse * wj));
                }
            }
        }
    }
}

void DrawBentPlane(float bend, float thickness, Color color, float aspect)
{
    rlBegin(RL_QUADS);
    rlColor4ub(color.r, color.g, color.b, color.a);
    const int segments = 32;
    for (int i = 0; i < segments; ++i)
    {
        float x0 = -0.5f + static_cast<float>(i) / segments, x1 = -0.5f + static_cast<float>(i + 1) / segments;
        float t = thickness * 0.5f;
        Vector3 a = BentPoint(x0, -0.5f, t, bend, aspect), b = BentPoint(x0, 0.5f, t, bend, aspect);
        Vector3 c = BentPoint(x1, 0.5f, t, bend, aspect), d = BentPoint(x1, -0.5f, t, bend, aspect);
        Vector3 e = BentPoint(x0, -0.5f, -t, bend, aspect), f = BentPoint(x0, 0.5f, -t, bend, aspect);
        Vector3 g = BentPoint(x1, 0.5f, -t, bend, aspect), h = BentPoint(x1, -0.5f, -t, bend, aspect);
        SurfaceQuad(a, b, c, d); SurfaceQuad(h, g, f, e);
        SurfaceQuad(e, a, d, h); SurfaceQuad(b, f, g, c);
        if (fabsf(bend) < 359.999f && i == 0) SurfaceQuad(e, f, b, a);
        if (fabsf(bend) < 359.999f && i == segments - 1) SurfaceQuad(d, c, g, h);
    }
    rlEnd();
}

void DrawDonut(Color color)
{
    rlBegin(RL_QUADS);
    rlColor4ub(color.r, color.g, color.b, color.a);
    for (int i = 0; i < 32; ++i)
        for (int j = 0; j < 16; ++j)
        {
            auto point = [](float a, float b) -> Vector3 {
                float r = 0.35f + 0.15f * cosf(b);
                return {r * cosf(a), 0.15f * sinf(b), r * sinf(a)};
            };
            float a = i * 2 * PI / 32, an = (i + 1) * 2 * PI / 32;
            float b = j * 2 * PI / 16, bn = (j + 1) * 2 * PI / 16;
            SurfaceQuad(point(a, b), point(a, bn), point(an, bn), point(an, b));
        }
    rlEnd();
}

void DrawPart(const Part& part)
{
    Color color = part.color;
    bool reversed = part.mirrorX ^ part.mirrorY ^ part.mirrorZ;
    if (reversed) { rlDrawRenderBatchActive(); rlDisableBackfaceCulling(); }
    rlPushMatrix();
    rlTranslatef(part.position.x, part.position.y, part.position.z);
    rlRotatef(part.rotation.y, 0, 1, 0);
    rlRotatef(part.rotation.x, 1, 0, 0);
    rlRotatef(part.rotation.z, 0, 0, 1);
    rlScalef(part.size.x * (part.mirrorX ? -1.0f : 1.0f), part.size.y * (part.mirrorY ? -1.0f : 1.0f), part.size.z * (part.mirrorZ ? -1.0f : 1.0f));
    switch (part.type)
    {
        case 0: DrawCube({0, 0, 0}, 1, 1, 1, color); break;
        case 1: DrawSphere({0, 0, 0}, 0.5f, color); break;
        case 2: DrawCylinder({0, -0.5f, 0}, 0, 0.5f, 1, 24, color); break;
        case 3: DrawCylinder({0, -0.5f, 0}, 0.5f, 0.5f, 1, 24, color); break;
        case 4: DrawBentPlane(part.bend, part.thickness, color, part.size.x / part.size.y); break;
        case 13: DrawDonut(color); break;
        default:
            if (part.type >= 5 && part.type < 5 + modelCount)
            {
                rlTranslatef(0, -1.25f, 0);
                DrawModel(part.type - 5);
            }
            break;
    }
    rlPopMatrix();
    if (reversed) { rlDrawRenderBatchActive(); rlEnableBackfaceCulling(); }
}

void DrawModel(int selected)
{
    const Color iron = {175, 175, 175, 255};
    const Color dark = {125, 125, 125, 255};
    const Color light = {220, 220, 220, 255};
    switch (selected)
    {
        case 0:
            DrawSphereEx({0, 1.5f, 0}, 0.43f, 16, 20, iron);
            DrawSphere({0, 1.13f, 0}, 0.34f, dark);
            DrawCylinderEx({0, 1.1f, 0}, {0, 1.55f, 0}, 0.32f, 0.39f, 16, iron);
            DrawCylinder({0, 1.83f, 0}, 0.13f, 0.15f, 0.18f, 12, dark);
            DrawSphere({0, 2.2f, 0}, 0.28f, light);
            for (float side : {-1.0f, 1.0f})
            {
                DrawSphere({side * 0.44f, 1.65f, 0}, 0.19f, iron);
                DrawCylinderEx({side * 0.44f, 1.61f, 0}, {side * 0.56f, 1.22f, 0}, 0.16f, 0.12f, 12, iron);
                DrawSphere({side * 0.56f, 1.2f, 0}, 0.13f, dark);
                DrawCylinderEx({side * 0.56f, 1.2f, 0}, {side * 0.6f, 0.88f, 0.06f}, 0.12f, 0.09f, 12, iron);
                DrawSphere({side * 0.6f, 0.84f, 0.06f}, 0.11f, light);
                DrawSphere({side * 0.2f, 1.02f, 0}, 0.18f, dark);
                DrawCylinderEx({side * 0.2f, 1.0f, 0}, {side * 0.23f, 0.58f, 0}, 0.17f, 0.13f, 12, iron);
                DrawSphere({side * 0.23f, 0.56f, 0}, 0.14f, dark);
                DrawCylinderEx({side * 0.23f, 0.55f, 0}, {side * 0.23f, 0.18f, 0}, 0.13f, 0.1f, 12, iron);
                DrawSphere({side * 0.23f, 0.13f, 0.08f}, 0.13f, dark);
            }
            break;
        case 1:
            // Capucha de malla estilizada: cabeza, faldon y abertura facial oscura.
            DrawSphere({0, 1.8f, 0}, 0.55f, iron);
            DrawHoodSkirt();
            DrawSphere({0, 1.75f, 0.46f}, 0.31f, {25, 25, 25, 255});
            for (int y = 0; y < 6; ++y)
                for (int i = 0; i < 16; ++i)
                {
                    float angle = (i + (y % 2) * 0.5f) * 2.0f * PI / 16;
                    float radius = 0.56f + (5 - y) * 0.065f;
                    DrawSphere({radius * sinf(angle), 0.9f + y * 0.13f, radius * cosf(angle)}, 0.035f, light);
                }
            break;
        case 2:
            DrawCylinder({0, 0.15f, 0}, 0.10f, 0.07f, 1.55f, 12, {145, 92, 55, 255});
            DrawCylinder({0, 1.55f, 0}, 0.3f, 0.14f, 0.3f, 12, {122, 106, 82, 255});
            DrawSphere({0, 1.97f, 0}, 0.3f, {255, 184, 75, 255});
            DrawCylinder({0, 1.95f, 0}, 0.0f, 0.27f, 0.65f, 12, {255, 184, 75, 255});
            break;
        case 3:
            DrawSphereEx({0, 0.75f, 0}, 0.55f, 16, 20, iron);
            DrawSphere({0, 1.35f, 0.18f}, 0.38f, light);
            DrawCylinder({-0.24f, 1.58f, 0.18f}, 0.0f, 0.16f, 0.35f, 4, iron);
            DrawCylinder({0.24f, 1.58f, 0.18f}, 0.0f, 0.16f, 0.35f, 4, iron);
            for (float side : {-1.0f, 1.0f})
            {
                DrawSphere({side * 0.17f, 1.43f, 0.49f}, 0.09f, {187, 202, 132, 255});
                DrawSphere({side * 0.17f, 1.43f, 0.56f}, 0.04f, {22, 22, 22, 255});
                DrawSphere({side * 0.10f, 1.23f, 0.48f}, 0.12f, {230, 225, 215, 255});
                for (int w = 0; w < 3; ++w)
                    DrawCylinderEx({side * 0.16f, 1.24f, 0.54f}, {side * 0.52f, 1.19f + w * 0.06f, 0.54f}, 0.012f, 0.008f, 5, light);
            }
            DrawSphere({0, 1.3f, 0.59f}, 0.055f, {190, 130, 130, 255});
            DrawCylinderEx({0.43f, 0.45f, -0.25f}, {0.85f, 1.15f, -0.35f}, 0.13f, 0.07f, 12, iron);
            break;
        case 4:
            DrawCube({0, 1.55f, 0}, 0.22f, 1.35f, 0.08f, light);
            DrawCylinder({0, 2.2f, 0}, 0, 0.12f, 0.3f, 4, light);
            DrawCube({0, 0.88f, 0}, 0.65f, 0.12f, 0.16f, iron);
            DrawCylinder({0, 0.35f, 0}, 0.08f, 0.09f, 0.53f, 12, dark);
            DrawSphere({0, 0.3f, 0}, 0.12f, iron);
            break;
        case 5:
            DrawSphere({0, 0.65f, 0}, 0.48f, dark);
            DrawSphere({0, 0.8f, 0.45f}, 0.3f, iron);
            DrawSphere({-0.2f, 1.02f, 0.42f}, 0.13f, light);
            DrawSphere({0.2f, 1.02f, 0.42f}, 0.13f, light);
            DrawSphere({-0.12f, 0.85f, 0.69f}, 0.045f, {30, 30, 30, 255});
            DrawSphere({0.12f, 0.85f, 0.69f}, 0.045f, {30, 30, 30, 255});
            DrawSphere({0, 0.72f, 0.75f}, 0.06f, {210, 145, 145, 255});
            DrawCylinderEx({0, 0.55f, -0.4f}, {0.5f, 0.3f, -1.0f}, 0.06f, 0.025f, 10, light);
            break;
        case 6:
            DrawModel(0);
            DrawSphere({0, 2.2f, 0.17f}, 0.25f, {140, 160, 120, 255});
            DrawSphere({-0.11f, 2.25f, 0.39f}, 0.05f, {35, 25, 25, 255});
            DrawSphere({0.11f, 2.25f, 0.39f}, 0.05f, {35, 25, 25, 255});
            break;
        case 7:
            rlPushMatrix();
            rlTranslatef(0, 1.3f, 0);
            rlScalef(0.85f, 1.15f, 0.18f);
            DrawSphere({0, 0, 0}, 0.7f, iron);
            rlPopMatrix();
            DrawSphere({0, 1.3f, 0.13f}, 0.16f, light);
            DrawCube({0, 1.3f, 0.14f}, 0.07f, 1.2f, 0.05f, dark);
            break;
    }
}

int main()
{
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);
    InitWindow(1280, 800, "AsciiForge - catalogo de modelos");
    SetExitKey(KEY_NULL);
    SetWindowMinSize(800, 600);
    SetTargetFPS(60);

    // Fuente monoespaciada del sistema con el glifo pilcrow (¶).
    int glyphs[96];
    for (int i = 0; i < 95; ++i) glyphs[i] = 32 + i;
    glyphs[95] = 0x00B6;
    const char* fontPath = "/usr/share/fonts/noto/NotoSansMono-Regular.ttf";
    if (!FileExists(fontPath)) fontPath = "/usr/share/fonts/TTF/DejaVuSansMono.ttf";
    Font asciiFont = LoadFontEx(fontPath, 32, glyphs, 96);
    if (asciiFont.texture.id == 0)
    {
        TraceLog(LOG_ERROR, "No se encontro una fuente monoespaciada compatible con ¶");
        CloseWindow();
        return 1;
    }

    const char* vertexShader = R"(
        #version 330
        in vec3 vertexPosition;
        in vec3 vertexNormal;
        in vec4 vertexColor;
        uniform mat4 mvp;
        uniform mat4 matModel;
        out vec3 normal;
        out vec3 worldPosition;
        out vec4 color;
        void main() {
            normal = normalize(transpose(inverse(mat3(matModel))) * vertexNormal);
            worldPosition = (matModel * vec4(vertexPosition, 1.0)).xyz;
            color = vertexColor;
            gl_Position = mvp * vec4(vertexPosition, 1.0);
        }
    )";
    const char* fragmentShader = R"(
        #version 330
        in vec3 normal;
        in vec3 worldPosition;
        in vec4 color;
        uniform vec3 lightPosition;
        uniform vec3 extraPositions[8];
        uniform vec3 extraColors[8];
        uniform float extraStrengths[8];
        uniform int extraOccluders[8];
        uniform int extraCount;
        uniform vec3 boundsMin[32];
        uniform vec3 boundsMax[32];
        uniform int boundsType[32];
        uniform mat4 inverseTransforms[32];
        uniform int boundsCount;
        uniform int selfEmissive;
        uniform int receiverOccluder;
        out vec4 finalColor;
        float shapeDistance(vec3 p, int kind) {
            if (kind == 2) {
                vec2 d = abs(vec2(length(p.xz), p.y)) - vec2(0.5, 0.5);
                return min(max(d.x, d.y), 0.0) + length(max(d, 0.0));
            }
            if (kind == 3) {
                vec2 q = vec2(length(p.xz), p.y);
                vec2 k1 = vec2(0.0, 0.5), k2 = vec2(-0.5, 1.0);
                vec2 ca = vec2(q.x - min(q.x, q.y < 0.0 ? 0.5 : 0.0), abs(q.y) - 0.5);
                vec2 cb = q - k1 + k2 * clamp(dot(k1 - q, k2) / dot(k2, k2), 0.0, 1.0);
                float signValue = cb.x < 0.0 && ca.y < 0.0 ? -1.0 : 1.0;
                return signValue * sqrt(min(dot(ca, ca), dot(cb, cb)));
            }
            return length(vec2(length(p.xz) - 0.35, p.y)) - 0.15;
        }
        bool blocked(vec3 start, vec3 light, int ignoreIndex) {
            vec3 delta = light - start;
            float distanceToLight = length(delta);
            if (distanceToLight < 0.04) return false;
            vec3 direction = delta / distanceToLight;
            for (int i = 0; i < 32; ++i) {
                if (i >= boundsCount) break;
                if (i == ignoreIndex || i == receiverOccluder) continue;
                float nearT = 0.015;
                float farT = distanceToLight - 0.025;
                if (boundsType[i] == 1) {
                    vec3 center = (boundsMin[i] + boundsMax[i]) * 0.5;
                    vec3 radius = max((boundsMax[i] - boundsMin[i]) * 0.5, vec3(0.0001));
                    vec3 o = (start - center) / radius;
                    vec3 d = direction / radius;
                    float a = dot(d, d), b = dot(o, d), c = dot(o, o) - 1.0;
                    float disc = b*b - a*c;
                    if (disc >= 0.0) {
                        float root = sqrt(disc);
                        float t0 = (-b - root) / a, t1 = (-b + root) / a;
                        if (t1 > nearT && t0 < farT) return true;
                    }
                } else {
                    bool hit = true;
                    for (int axis = 0; axis < 3; ++axis) {
                        if (abs(direction[axis]) < 0.00001) {
                            if (start[axis] < boundsMin[i][axis] || start[axis] > boundsMax[i][axis]) hit = false;
                        } else {
                            float t0 = (boundsMin[i][axis] - start[axis]) / direction[axis];
                            float t1 = (boundsMax[i][axis] - start[axis]) / direction[axis];
                            nearT = max(nearT, min(t0, t1));
                            farT = min(farT, max(t0, t1));
                        }
                    }
                    if (hit && nearT < farT) {
                        if (boundsType[i] >= 2) {
                            vec3 origin = (inverseTransforms[i] * vec4(start, 1.0)).xyz;
                            vec3 localDirection = (inverseTransforms[i] * vec4(direction, 0.0)).xyz;
                            float speed = max(length(localDirection), 0.00001);
                            float t = nearT;
                            for (int step = 0; step < 96; ++step) {
                                if (t >= farT) break;
                                float d = shapeDistance(origin + localDirection * t, boundsType[i]);
                                if (d < 0.001) return true;
                                t += max(d * 0.8 / speed, 0.0001);
                            }
                        } else return true;
                    }
                }
            }
            return false;
        }
        void main() {
            vec3 toLight = lightPosition - worldPosition;
            float distanceToLight = length(toLight);
            float diffuse = max(dot(normalize(normal), normalize(toLight)), 0.0);
            float attenuation = 1.0 / (1.0 + 0.035 * distanceToLight * distanceToLight);
            vec3 shadowStart = worldPosition + normalize(normal) * 0.02;
            if (diffuse > 0.0 && blocked(shadowStart, lightPosition, -1)) diffuse = 0.0;
            float brightness = 0.12 + 0.72 * diffuse * attenuation;
            vec3 lightTint = vec3(0.0);
            for (int i = 0; i < extraCount; ++i) {
                vec3 direction = extraPositions[i] - worldPosition;
                float d = length(direction);
                float contribution = extraStrengths[i] * max(dot(normalize(normal), direction / max(d, 0.001)), 0.0) / (1.0 + 0.5 * d * d);
                if (contribution > 0.0 && blocked(shadowStart, extraPositions[i], extraOccluders[i])) contribution = 0.0;
                brightness += 0.9 * contribution;
                lightTint += extraColors[i] * contribution * 0.35;
            }
            if (selfEmissive != 0) brightness = 1.0;
            finalColor = vec4(min(color.rgb * (vec3(1.0) + lightTint), vec3(1.0)), clamp(brightness, 0.0, 1.0));
        }
    )";
    Shader lighting = LoadShaderFromMemory(vertexShader, fragmentShader);
    int lightLocation = GetShaderLocation(lighting, "lightPosition");
    int positionsLocation = GetShaderLocation(lighting, "extraPositions[0]");
    int colorsLocation = GetShaderLocation(lighting, "extraColors[0]");
    int strengthsLocation = GetShaderLocation(lighting, "extraStrengths[0]");
    int emitterOccludersLocation = GetShaderLocation(lighting, "extraOccluders[0]");
    int boundsMinLocation = GetShaderLocation(lighting, "boundsMin[0]");
    int boundsMaxLocation = GetShaderLocation(lighting, "boundsMax[0]");
    int boundsTypeLocation = GetShaderLocation(lighting, "boundsType[0]");
    int boundsCountLocation = GetShaderLocation(lighting, "boundsCount");
    int inverseLocations[32];
    for (int i = 0; i < 32; ++i) inverseLocations[i] = GetShaderLocation(lighting, TextFormat("inverseTransforms[%i]", i));
    int countLocation = GetShaderLocation(lighting, "extraCount");
    int selfLocation = GetShaderLocation(lighting, "selfEmissive");
    int receiverLocation = GetShaderLocation(lighting, "receiverOccluder");
    const char* maskFragment = R"(
        #version 330
        in vec4 color;
        uniform int isEmitter;
        out vec4 finalColor;
        void main() { finalColor = vec4(isEmitter != 0 ? color.rgb : vec3(0.0), 1.0); }
    )";
    Shader maskShader = LoadShaderFromMemory(vertexShader, maskFragment);
    int maskEmitterLocation = GetShaderLocation(maskShader, "isEmitter");
    RenderTexture2D scene = LoadRenderTexture(columns, rows);
    RenderTexture2D mask = LoadRenderTexture(columns, rows);
    if (scene.id == 0 || mask.id == 0 || lighting.id == 0 || maskShader.id == 0)
    {
        TraceLog(LOG_ERROR, "No se pudo crear el renderizador");
        if (scene.id != 0) UnloadRenderTexture(scene);
        if (mask.id != 0) UnloadRenderTexture(mask);
        if (lighting.id != 0) UnloadShader(lighting);
        if (maskShader.id != 0) UnloadShader(maskShader);
        UnloadFont(asciiFont);
        CloseWindow();
        return 1;
    }

    Camera3D camera{};
    camera.target = {0, 1.25f, 0};
    camera.up = {0, 1, 0};
    camera.fovy = 42;
    camera.projection = CAMERA_PERSPECTIVE;
    float yaw = 0.35f, pitch = 0.2f, distance = 4.0f;
    double lastViewportClick = -1.0;
    Vector2 lastViewportClickPosition{};
    int selected = 0;
    bool editing = false;
    bool movingLight = false;
    Vector3 lightPosition = {-2.0f, 3.5f, 3.0f};
    std::vector<Part> parts;
    int selectedPart = -1;
    std::vector<int> selection;
    std::vector<Part> clipboard;
    std::vector<EditorState> undoHistory, redoHistory;
    bool continuousPrevious = false;
    int textSize = 13; // Decimas de escala, de 0.5 a 8.0.
    int density = 10; // Decimas de densidad, de 0.5 a 5.0.
    bool showColorPicker = false;
    bool showPlanePanel = false;
    bool showPhysicsPanel = false;
    bool physicsRunning = false;
    std::vector<Part> previewParts;
    std::vector<Vector3> previewVelocity;
    int face = 2; // +X, -X, +Y, -Y, +Z, -Z.
    Tool tool = Tool::Select;
    std::string status = "";
    std::string lastFile;
    std::string editingFile;
    int editingExample = -1;
    std::vector<Part> catalogParts;
    std::string catalogFile;
    std::string catalogStatus;
    bool showingSaved = false;
    int catalogExample = -1;
    int viewingExample = -1;
    bool showSavedMenu = false;
    int savedScroll = 0;
    std::vector<std::filesystem::path> savedFiles;
    double confirmLoadUntil = 0;
    double confirmEditUntil = 0;
    const Color gold = {207, 209, 204, 255};
    const Color muted = {110, 115, 112, 255};

    auto showExample = [&](int index)
    {
        viewingExample = index;
        showingSaved = false;
        catalogExample = -1;
    };
    showExample(selected);

    enum class Dialog { None, DeleteOne, DeleteAll, Exit };
    Dialog dialog = Dialog::None;
    std::vector<std::filesystem::path> deleteTargets;
    std::string catalogNotice;
    double noticeUntil = 0;
    bool exitRequested = false;
    while (!exitRequested && !WindowShouldClose())
    {
        int width = GetScreenWidth(), height = GetScreenHeight();
        if (dialog != Dialog::None)
        {
            Rectangle box = {width / 2.0f - 300, height / 2.0f - 125, 600, 250};
            Rectangle cancel = {box.x + 55, box.y + 180, 190, 42};
            Rectangle confirm = {box.x + 355, box.y + 180, 190, 42};
            Vector2 mouse = GetMousePosition();
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, cancel)) dialog = Dialog::None;
            else if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, confirm))
            {
                if (dialog == Dialog::Exit) exitRequested = true;
                else
                {
                    int removed = 0, failed = 0;
                    bool editingFileDeleted = false;
                    for (const auto& path : deleteTargets)
                    {
                        std::error_code error;
                        if (std::filesystem::remove(path, error))
                        {
                            ++removed;
                            if (path.filename().string() == editingFile) editingFileDeleted = true;
                        }
                        else ++failed;
                    }
                    if (editingFileDeleted)
                    {
                        editingFile.clear();
                        editingExample = -1;
                        status = "Archivo eliminado; guarda el taller como una copia nueva";
                    }
                    showExample(selected);
                    showSavedMenu = false;
                    catalogNotice = std::to_string(removed) + " modelo(s) eliminado(s)";
                    if (failed) catalogNotice += "; " + std::to_string(failed) + " no se pudieron eliminar";
                    noticeUntil = GetTime() + 8.0;
                    dialog = Dialog::None;
                }
            }
            if (dialog != Dialog::None)
            {
                BeginDrawing();
                ClearBackground({8, 9, 9, 255});
                DrawRectangleRec(box, {27, 30, 31, 255});
                DrawRectangleLinesEx(box, 1, muted);
                DrawText(dialog == Dialog::Exit ? "SALIR DE ASCII FORGE" : "CONFIRMAR ELIMINACION", box.x + 25, box.y + 22, 20, gold);
                if (dialog == Dialog::Exit)
                {
                    DrawText("Los cambios no guardados se perderan.", box.x + 25, box.y + 68, 16, gold);
                    DrawText("Quieres salir del programa?", box.x + 25, box.y + 96, 16, gold);
                }
                else
                {
                    DrawText(dialog == Dialog::DeleteOne ? "Se eliminara el modelo guardado seleccionado." : "Se eliminaran TODOS los modelos guardados.", box.x + 25, box.y + 68, 16, gold);
                    DrawText(TextFormat("Archivos: %i  |  Accion irreversible.", static_cast<int>(deleteTargets.size())), box.x + 25, box.y + 97, 16, {225, 135, 120, 255});
                    std::string detail = dialog == Dialog::DeleteOne && !deleteTargets.empty() ? deleteTargets.front().filename().string() : "Los ejemplos integrados se conservan.";
                    if (detail.size() > 55) detail = detail.substr(0, 52) + "...";
                    DrawText(detail.c_str(), box.x + 25, box.y + 125, 14, muted);
                }
                DrawPixelButton(cancel, {45, 53, 59, 255});
                DrawPixelButton(confirm, {103, 50, 45, 255});
                DrawText("CANCELAR", cancel.x + 48, cancel.y + 13, 15, gold);
                DrawText(dialog == Dialog::Exit ? "SALIR" : "SI, ELIMINAR", confirm.x + 40, confirm.y + 13, 15, gold);
                EndDrawing();
                continue;
            }
            if (exitRequested) break;
            // El clic de confirmacion o cancelacion no debe activar el visor.
            continue;
        }
        Rectangle viewport = {20, 175, static_cast<float>(width - 40), static_cast<float>(height - 305)};
        Rectangle modeButton = {static_cast<float>(width - 200), 8, 180, 32};
        Rectangle lightButton = {static_cast<float>(width - 355), 8, 145, 32};
        Rectangle savedButton = {245, 8, 155, 32};
        Rectangle savedMenu = {245, 42, 230, 200};
        Rectangle left = {viewport.x + 20, viewport.y + viewport.height - 54, 46, 36};
        Rectangle right = {viewport.x + viewport.width - 66, viewport.y + viewport.height - 54, 46, 36};
        Rectangle tools[4];
        Rectangle primitives[5];
        for (int i = 0; i < 4; ++i) tools[i] = {20 + i * 70.0f, 45, 66, 26};
        for (int i = 0; i < 5; ++i) primitives[i] = {20 + i * 74.0f, 138, 70, 26};
        Rectangle saveButton = {385, 76, 70, 26};
        Rectangle loadButton = {460, 76, 70, 26};
        Rectangle colorButton = {440, 45, 70, 26};
        Rectangle colorPanel = {447, 171, 245, 142};
        Rectangle editActions[6];
        for (int i = 0; i < 6; ++i) editActions[i] = {20 + i * 60.0f, 76, 56, 26};
        Rectangle donutButton = {390, 138, 70, 26};
        Rectangle mirrorButtons[3];
        for (int i = 0; i < 3; ++i) mirrorButtons[i] = {20 + i * 95.0f, 107, 90, 26};
        Rectangle hideLightButton = {310, 107, 130, 26};
        Rectangle lightStrengthSlider = {static_cast<float>(width - 175), 111, 145, 18};
        Rectangle faceButton = {300, 45, 66, 26};
        Rectangle planeButton = {370, 45, 66, 26};
        Rectangle planePanel = {447, 171, 250, 180};
        Rectangle physicsButton = {static_cast<float>(width - 285), 138, 125, 26};
        Rectangle previewButton = {static_cast<float>(width - 150), 138, 130, 26};
        Rectangle physicsPanel = {447, 171, 250, 235};
        Rectangle fixedButton = {physicsPanel.x + 10, physicsPanel.y + 195, 220, 27};
        Rectangle actionArea = {0, 44, static_cast<float>(width), 126};
        Rectangle emissionButton = {static_cast<float>(width - 190), static_cast<float>(height - 125), 150, 30};
        Rectangle rigidButton = {static_cast<float>(width - 350), static_cast<float>(height - 125), 150, 30};
        Rectangle editButton = {static_cast<float>(width - 190), static_cast<float>(height - 125), 150, 30};
        Rectangle textSlider = {static_cast<float>(width - 175), 49, 145, 18};
        Rectangle densitySlider = {static_cast<float>(width - 175), 79, 145, 18};
        Rectangle deleteOneButton = {20, 48, 150, 28};
        Rectangle deleteAllButton = {180, 48, 170, 28};
        Rectangle duplicateButton = {20, 79, 150, 28};
        Rectangle exitButton = {static_cast<float>(width - 115), static_cast<float>(height - 47), 95, 30};
        Vector2 mouse = GetMousePosition();
        bool clicked = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
        bool justEnteredEditor = false;
        bool selectionClicked = false;
        if (clicked && CheckCollisionPointRec(mouse, exitButton)) { dialog = Dialog::Exit; continue; }
        if (!editing && !showSavedMenu && clicked && CheckCollisionPointRec(mouse, deleteOneButton))
        {
            if (showingSaved && !catalogFile.empty())
            {
                deleteTargets = {ModelDirectory() / std::filesystem::path(catalogFile).filename()};
                dialog = Dialog::DeleteOne;
                continue;
            }
            catalogNotice = "El original integrado no se puede borrar. Duplica para editar.";
            noticeUntil = GetTime() + 6.0;
        }
        if (!editing && !showSavedMenu && clicked && CheckCollisionPointRec(mouse, deleteAllButton))
        {
            std::error_code error;
            deleteTargets = ListModels(error);
            if (!error && !deleteTargets.empty()) { dialog = Dialog::DeleteAll; continue; }
            catalogNotice = error && error != std::errc::no_such_file_or_directory ? "No se pudo leer la carpeta de modelos" : "No hay modelos guardados para eliminar";
            noticeUntil = GetTime() + 6.0;
        }
        if (!editing && !showSavedMenu && clicked && CheckCollisionPointRec(mouse, duplicateButton))
        {
            std::vector<Part> duplicate;
            if (showingSaved) duplicate = catalogParts;
            else duplicate.push_back({selected + 5, {0, 1.25f, 0}, {1, 1, 1}, 0});
            std::string duplicateFile, message;
            if (SaveModel(duplicate, duplicateFile, message, -1))
            {
                catalogParts = std::move(duplicate);
                catalogFile = duplicateFile;
                catalogExample = -1;
                viewingExample = -1;
                showingSaved = true;
                catalogNotice = "Copia creada: " + duplicateFile + ". Ya puedes editarla o borrarla.";
            }
            else catalogNotice = message;
            noticeUntil = GetTime() + 8.0;
        }
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, textSlider))
            textSize = std::clamp(5 + static_cast<int>(roundf((mouse.x - textSlider.x) / textSlider.width * 75)), 5, 80);
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, densitySlider))
            density = std::clamp(5 + static_cast<int>(roundf((mouse.x - densitySlider.x) / densitySlider.width * 45)), 5, 50);
        int desiredRows = density * 3;
        int desiredColumns = std::clamp(static_cast<int>(roundf(desiredRows * viewport.width / viewport.height)), 1, 512);
        if (desiredRows != rows || desiredColumns != columns)
        {
                RenderTexture2D replacement = LoadRenderTexture(desiredColumns, desiredRows);
                RenderTexture2D maskReplacement = LoadRenderTexture(desiredColumns, desiredRows);
                if (replacement.id != 0 && maskReplacement.id != 0)
                {
                    UnloadRenderTexture(scene);
                    UnloadRenderTexture(mask);
                    scene = replacement;
                    mask = maskReplacement;
                    columns = desiredColumns;
                    rows = desiredRows;
                }
                else
                {
                    if (replacement.id != 0) UnloadRenderTexture(replacement);
                    if (maskReplacement.id != 0) UnloadRenderTexture(maskReplacement);
                }
        }
        if (IsKeyPressed(KEY_M) || (clicked && CheckCollisionPointRec(mouse, modeButton))) { editing = !editing; if (editing) tool = Tool::Select; physicsRunning = false; showPhysicsPanel = false; showSavedMenu = false; showColorPicker = false; showPlanePanel = false; }
        bool previewToggled = false;
        if (editing && (IsKeyPressed(KEY_T) || (clicked && CheckCollisionPointRec(mouse, previewButton))))
        {
            previewToggled = true;
            if (physicsRunning) { physicsRunning = false; status = "Vista previa detenida; el modelo no ha cambiado"; }
            else if (std::none_of(parts.begin(), parts.end(), [](const Part& p) { return p.rigid && !p.physicsFixed; }))
                status = "Activa un cuerpo rigido movil para simular";
            else
            {
                previewParts = parts;
                previewVelocity.assign(parts.size(), {});
                physicsRunning = true;
                movingLight = false;
                showPhysicsPanel = false; showPlanePanel = false; showColorPicker = false;
                status = "Vista previa: T para detener; las posiciones no se guardan";
            }
        }
        if (!editing && clicked && CheckCollisionPointRec(mouse, savedButton))
        {
            showSavedMenu = !showSavedMenu;
            if (showSavedMenu)
            {
                std::error_code error;
                savedFiles = ListModels(error);
                savedScroll = 0;
                catalogStatus = error && error != std::errc::no_such_file_or_directory ? "No se pudieron listar los modelos" : "";
            }
        }
        bool menuInteraction = !editing && showSavedMenu && CheckCollisionPointRec(mouse, savedMenu);
        if (menuInteraction)
        {
            savedScroll = std::clamp(savedScroll - static_cast<int>(GetMouseWheelMove()), 0, std::max(0, static_cast<int>(savedFiles.size()) - 7));
            if (clicked)
            {
                int index = savedScroll + static_cast<int>((mouse.y - savedMenu.y - 5) / 26);
                if (mouse.y >= savedMenu.y + 5 && index >= 0 && index < static_cast<int>(savedFiles.size()) && index < savedScroll + 7)
                {
                    std::string requested = savedFiles[index].filename().string();
                    if (LoadNextModel(catalogParts, catalogFile, catalogStatus, requested, &catalogExample)) { showingSaved = true; viewingExample = -1; showSavedMenu = false; }
                }
            }
        }
        else if (showSavedMenu && clicked && !CheckCollisionPointRec(mouse, savedButton)) showSavedMenu = false;
        if (!editing && !menuInteraction && clicked && CheckCollisionPointRec(mouse, editButton))
        {
            if (!parts.empty() && GetTime() > confirmEditUntil)
            {
                confirmEditUntil = GetTime() + 4.0;
                catalogStatus = "El taller se sustituira; pulsa EDITAR otra vez para confirmar";
            }
            else
            {
                if (showingSaved)
                {
                    parts = catalogParts;
                    editingFile = catalogFile;
                    editingExample = catalogExample;
                    lastFile = catalogFile;
                    status = "Editando " + editingFile + "; Guardar actualiza el original";
                }
                else
                {
                    Part example{selected + 5, {0, 1.25f, 0}, {1, 1, 1}, 0};
                    parts = {example};
                    editingFile.clear();
                    editingExample = -1;
                    status = "Ejemplo agrupado; Guardar crea una copia editable";
                }
                selectedPart = parts.empty() ? -1 : 0;
                selection = selectedPart < 0 ? std::vector<int>{} : std::vector<int>{selectedPart};
                undoHistory.clear(); redoHistory.clear(); continuousPrevious = false;
                editing = true;
                tool = Tool::Select;
                justEnteredEditor = true;
                movingLight = false;
                showColorPicker = false;
                showSavedMenu = false;
                confirmEditUntil = 0;
            }
        }
        if (!physicsRunning && (IsKeyPressed(KEY_L) || (clicked && CheckCollisionPointRec(mouse, lightButton)))) movingLight = !movingLight;
        if (movingLight)
        {
            float step = GetFrameTime() * 2.0f;
            if (IsKeyDown(KEY_LEFT)) lightPosition.x -= step;
            if (IsKeyDown(KEY_RIGHT)) lightPosition.x += step;
            if (IsKeyDown(KEY_UP)) lightPosition.z -= step;
            if (IsKeyDown(KEY_DOWN)) lightPosition.z += step;
            if (IsKeyDown(KEY_Q)) lightPosition.y -= step;
            if (IsKeyDown(KEY_E)) lightPosition.y += step;
        }
        if (!editing && !movingLight)
        {
            if (clicked && !menuInteraction && CheckCollisionPointRec(mouse, left)) { selected = (selected + modelCount - 1) % modelCount; showExample(selected); }
            if (clicked && !menuInteraction && CheckCollisionPointRec(mouse, right)) { selected = (selected + 1) % modelCount; showExample(selected); }
            // Desplazar la camara lateralmente mueve el modelo en pantalla sin editarlo.
            Vector3 rightVector = {cosf(yaw), 0, -sinf(yaw)};
            float pan = ((IsKeyDown(KEY_LEFT) ? 1.0f : 0.0f) - (IsKeyDown(KEY_RIGHT) ? 1.0f : 0.0f)) * GetFrameTime();
            camera.target = Vector3Add(camera.target, Vector3Scale(rightVector, pan));
            Vector3 towardCamera = {sinf(yaw), 0, cosf(yaw)};
            float depth = ((IsKeyDown(KEY_DOWN) ? 1.0f : 0.0f) - (IsKeyDown(KEY_UP) ? 1.0f : 0.0f)) * GetFrameTime();
            camera.target = Vector3Add(camera.target, Vector3Scale(towardCamera, depth));
        }
        else if (editing && !movingLight && !justEnteredEditor && !physicsRunning && !previewToggled)
        {
            EditorState before{parts, selection, selectedPart};
            bool historyAction = false;
            bool resetHistory = false;
            bool control = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
            bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
            if (clicked && CheckCollisionPointRec(mouse, physicsButton))
            {
                if (selectedPart >= 0 && parts[selectedPart].rigid)
                {
                    showPhysicsPanel = !showPhysicsPanel;
                    showColorPicker = false; showPlanePanel = false;
                }
                else { showPhysicsPanel = false; status = "Selecciona un cuerpo rigido para configurar su fisica"; }
            }
            auto action = [&](int index) { return clicked && CheckCollisionPointRec(mouse, editActions[index]); };
            auto restore = [&](const EditorState& state) { parts = state.parts; selection = state.selection; selectedPart = state.active; showColorPicker = false; };
            if ((control && IsKeyPressed(KEY_Z) && !shift) || action(0))
            {
                if (!undoHistory.empty()) { redoHistory.push_back({parts, selection, selectedPart}); restore(undoHistory.back()); undoHistory.pop_back(); status = "Accion deshecha"; }
                historyAction = true;
            }
            if ((control && (IsKeyPressed(KEY_Y) || (shift && IsKeyPressed(KEY_Z)))) || action(1))
            {
                if (!redoHistory.empty()) { undoHistory.push_back({parts, selection, selectedPart}); restore(redoHistory.back()); redoHistory.pop_back(); status = "Accion rehecha"; }
                historyAction = true;
            }
            if ((control && IsKeyPressed(KEY_A)) || action(5))
            {
                selection.clear();
                for (int i = 0; i < static_cast<int>(parts.size()); ++i) selection.push_back(i);
                selectedPart = selection.empty() ? -1 : selection.back();
                status = "Todas las piezas seleccionadas";
            }
            bool copy = (control && IsKeyPressed(KEY_C)) || action(2);
            bool cut = (control && IsKeyPressed(KEY_X)) || action(3);
            bool paste = (control && IsKeyPressed(KEY_V)) || action(4);
            if ((copy || cut) && !selection.empty())
            {
                clipboard.clear();
                for (int i : selection) if (i >= 0 && i < static_cast<int>(parts.size())) clipboard.push_back(parts[i]);
                status = cut ? "Piezas cortadas" : "Piezas copiadas";
                if (cut)
                {
                    std::vector<int> sorted = selection;
                    std::sort(sorted.begin(), sorted.end(), std::greater<int>());
                    for (int i : sorted) parts.erase(parts.begin() + i);
                    selection.clear(); selectedPart = -1; showColorPicker = false;
                }
            }
            if (paste && !clipboard.empty())
            {
                int currentLights = static_cast<int>(std::count_if(parts.begin(), parts.end(), [](const Part& p) { return p.emissive; }));
                int copiedLights = static_cast<int>(std::count_if(clipboard.begin(), clipboard.end(), [](const Part& p) { return p.emissive; }));
                if (currentLights + copiedLights > 8 || parts.size() + clipboard.size() > 10000) status = "No se puede pegar: limite de piezas o de 8 luces";
                else
                {
                    selection.clear();
                    for (Part p : clipboard) { p.position.x += 0.2f; selection.push_back(static_cast<int>(parts.size())); parts.push_back(p); }
                    selectedPart = selection.back();
                    status = "Piezas pegadas";
                }
            }
            for (int i = 0; i < 4; ++i)
                if (IsKeyPressed(KEY_F1 + i) || (clicked && CheckCollisionPointRec(mouse, tools[i])))
                {
                    Tool requested = static_cast<Tool>(i);
                    tool = tool == requested ? Tool::Select : requested;
                }
            if ((control && IsKeyPressed(KEY_S)) || (clicked && CheckCollisionPointRec(mouse, saveButton)))
            {
                if (SaveModel(parts, editingFile, status, editingExample))
                {
                    catalogParts = parts;
                    catalogFile = editingFile;
                    lastFile = editingFile;
                    showingSaved = true;
                    catalogExample = editingExample;
                    viewingExample = -1;
                }
            }
            if ((control && IsKeyPressed(KEY_O)) || (clicked && CheckCollisionPointRec(mouse, loadButton)))
            {
                if (!parts.empty() && GetTime() > confirmLoadUntil)
                {
                    confirmLoadUntil = GetTime() + 4.0;
                    status = "Abrir sustituye el taller; pulsa ABRIR otra vez para confirmar";
                }
                else
                {
                    if (LoadNextModel(parts, lastFile, status, "", &editingExample)) { selectedPart = parts.empty() ? -1 : 0; selection = selectedPart < 0 ? std::vector<int>{} : std::vector<int>{selectedPart}; editingFile = lastFile; resetHistory = true; }
                    confirmLoadUntil = 0;
                }
            }
            auto addPrimitive = [&](int type)
            {
                if (parts.size() >= 10000) { status = "Se alcanzo el limite de piezas"; return; }
                Part added{type, {0, 1.25f, 0}, {1, 1, 1}, 0};
                std::vector<Part> trial = parts;
                trial.push_back(added);
                bool found = false;
                for (int attempt = 0; attempt < 41; ++attempt)
                {
                    int n = (attempt + 1) / 2;
                    trial.back().position.x = (attempt == 0 ? 0.0f : (attempt % 2 ? 1.0f : -1.0f) * n * 0.75f);
                    if (!HasNewCollision(parts, trial)) { found = true; break; }
                }
                if (!found) { status = "No hay espacio libre cercano para crear la pieza"; return; }
                parts.push_back(trial.back());
                selectedPart = static_cast<int>(parts.size()) - 1;
                selection = {selectedPart};
                if (parts.back().position.x != 0) status = "Pieza creada en un lugar libre";
            };
            for (int i = 0; i < 5; ++i)
            {
                if (IsKeyPressed(KEY_ONE + i) || (clicked && CheckCollisionPointRec(mouse, primitives[i])))
                    addPrimitive(i);
            }
            if (IsKeyPressed(KEY_SIX) || (clicked && CheckCollisionPointRec(mouse, donutButton)))
                addPrimitive(13);
            for (int axis = 0; axis < 3; ++axis)
                if (clicked && CheckCollisionPointRec(mouse, mirrorButtons[axis]) && !selection.empty())
                {
                    for (int index : selection)
                    {
                        if (axis == 0) parts[index].mirrorX = !parts[index].mirrorX;
                        if (axis == 1) parts[index].mirrorY = !parts[index].mirrorY;
                        if (axis == 2) parts[index].mirrorZ = !parts[index].mirrorZ;
                    }
                    status = "Reflejo aplicado a la seleccion";
                }
            if (!parts.empty() && IsKeyPressed(KEY_TAB))
            {
                selectedPart = (selectedPart + 1) % static_cast<int>(parts.size());
                if (!shift) selection = {selectedPart};
                else if (std::find(selection.begin(), selection.end(), selectedPart) == selection.end()) selection.push_back(selectedPart);
            }
            if (clicked && CheckCollisionPointRec(mouse, viewport) && !CheckCollisionPointRec(mouse, actionArea) &&
                !(showColorPicker && CheckCollisionPointRec(mouse, colorPanel)) && !(showPlanePanel && CheckCollisionPointRec(mouse, planePanel)) && !(showPhysicsPanel && CheckCollisionPointRec(mouse, physicsPanel)))
            {
                int nearest = -1;
                float best = 12.0f;
                for (int i = 0; i < static_cast<int>(parts.size()); ++i)
                {
                    Vector2 projected = GetWorldToScreenEx(parts[i].position, camera, columns, rows);
                    if (projected.x < 0 || projected.x >= columns || projected.y < 0 || projected.y >= rows) continue;
                    float px = viewport.x + projected.x * viewport.width / columns;
                    float py = viewport.y + projected.y * viewport.height / rows;
                    float d = Vector2Distance(mouse, {px, py});
                    if (d < best) { best = d; nearest = i; }
                }
                if (nearest >= 0)
                {
                    selectionClicked = true;
                    auto found = std::find(selection.begin(), selection.end(), nearest);
                    if (shift || control)
                    {
                        if (found == selection.end()) selection.push_back(nearest);
                        else selection.erase(found);
                        selectedPart = selection.empty() ? -1 : selection.back();
                    }
                    else { selection = {nearest}; selectedPart = nearest; }
                }
            }
            if (selectedPart >= 0 && selectedPart < static_cast<int>(parts.size()))
            {
                if (IsKeyPressed(KEY_DELETE) || IsKeyPressed(KEY_BACKSPACE))
                {
                    std::vector<int> sorted = selection;
                    std::sort(sorted.begin(), sorted.end(), std::greater<int>());
                    for (int i : sorted) parts.erase(parts.begin() + i);
                    selectedPart = -1; selection.clear(); showColorPicker = false;
                }
                else
                {
                    Part& part = parts[selectedPart];
                    if ((!control && IsKeyPressed(KEY_C)) || (clicked && CheckCollisionPointRec(mouse, colorButton)))
                    {
                        if (part.type >= 5 && part.type <= 12) { showColorPicker = false; status = "El ejemplo agrupado conserva sus colores originales"; }
                        else { showColorPicker = !showColorPicker; showPlanePanel = false; showPhysicsPanel = false; }
                    }
                    if (IsKeyPressed(KEY_P) || (clicked && CheckCollisionPointRec(mouse, planeButton)))
                    {
                        if (part.type == 4) { showPlanePanel = !showPlanePanel; showColorPicker = false; showPhysicsPanel = false; }
                        else { showPlanePanel = false; status = "Selecciona un plano para ajustar su forma"; }
                    }
                    if (IsKeyPressed(KEY_F) || (clicked && CheckCollisionPointRec(mouse, faceButton)))
                    {
                        if (part.type == 0) face = (face + 1) % 6;
                        else if (part.type == 3 || (part.type == 4 && fabsf(part.bend) < 0.001f)) face = face == 2 ? 3 : 2;
                        else status = "Seleccion de caras: cubos, cilindros y planos rectos";
                    }
                    if (showPlanePanel && part.type == 4 && IsMouseButtonDown(MOUSE_BUTTON_LEFT))
                    {
                        for (int channel = 0; channel < 4; ++channel)
                        {
                            Rectangle slider = {planePanel.x + 73, planePanel.y + 28 + channel * 32.0f, 130, 22};
                            if (!CheckCollisionPointRec(mouse, slider)) continue;
                            float t = std::clamp((mouse.x - slider.x) / slider.width, 0.0f, 1.0f);
                            if (channel == 0) part.bend = roundf(-360 + 720 * t);
                            if (channel == 1) part.size.x = 0.04f + t * 4.96f;
                            if (channel == 2) part.size.z = 0.04f + t * 4.96f;
                            if (channel == 3) part.thickness = 0.001f + t * 0.499f;
                        }
                    }
                    if (showColorPicker && IsMouseButtonDown(MOUSE_BUTTON_LEFT))
                    {
                        for (int channel = 0; channel < 3; ++channel)
                        {
                            Rectangle slider = {colorPanel.x + 35, colorPanel.y + 32 + channel * 30.0f, 150, 20};
                            if (CheckCollisionPointRec(mouse, slider))
                            {
                                unsigned char value = static_cast<unsigned char>(std::clamp(static_cast<int>(roundf((mouse.x - slider.x) / slider.width * 255)), 0, 255));
                                if (channel == 0) part.color.r = value;
                                if (channel == 1) part.color.g = value;
                                if (channel == 2) part.color.b = value;
                            }
                        }
                    }
                    if (IsKeyPressed(KEY_B) || (clicked && CheckCollisionPointRec(mouse, emissionButton)))
                    {
                        int emitters = static_cast<int>(std::count_if(parts.begin(), parts.end(), [](const Part& p) { return p.emissive; }));
                        if (!part.emissive && emitters >= 8) status = "Maximo 8 piezas luminosas por modelo";
                        else { part.emissive = !part.emissive; if (!part.emissive) part.lightHidden = false; status = part.emissive ? "Pieza luminosa activada" : "Pieza luminosa desactivada"; }
                    }
                    if (IsKeyPressed(KEY_R) || (clicked && CheckCollisionPointRec(mouse, rigidButton)))
                    {
                        part.rigid = !part.rigid;
                        status = part.rigid ? "Cuerpo rigido activado" : "Cuerpo rigido desactivado";
                    }
                    if (showPhysicsPanel && part.rigid)
                    {
                        if (clicked && CheckCollisionPointRec(mouse, fixedButton)) part.physicsFixed = !part.physicsFixed;
                        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT))
                            for (int channel = 0; channel < 5; ++channel)
                            {
                                Rectangle slider = {physicsPanel.x + 90, physicsPanel.y + 35 + channel * 30.0f, 110, 20};
                                if (CheckCollisionPointRec(mouse, slider))
                                {
                                    float value = std::clamp((mouse.x - slider.x) / slider.width, 0.0f, 1.0f);
                                    if (channel == 0) part.bounce = roundf(value * 150) / 100.0f;
                                    if (channel == 1) { part.hardness = roundf(value * 200) / 100.0f; part.softness = 1.0f - std::min(part.hardness, 1.0f); }
                                    if (channel == 2) part.mass = std::clamp(powf(10.0f, -3.0f + value * 6.0f), 0.001f, 1000.0f);
                                    if (channel == 3) part.airDrag = roundf(value * 200) / 100.0f;
                                    if (channel == 4) part.friction = roundf(value * 100) / 100.0f;
                                }
                            }
                    }
                    if (part.emissive && (IsKeyPressed(KEY_H) || (clicked && CheckCollisionPointRec(mouse, hideLightButton))))
                    {
                        part.lightHidden = !part.lightHidden;
                        status = part.lightHidden ? "Cuerpo oculto; la luz sigue activa" : "Cuerpo luminoso visible";
                    }
                    if (part.emissive && IsMouseButtonDown(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, lightStrengthSlider))
                        part.lightStrength = std::clamp(roundf((mouse.x - lightStrengthSlider.x) / lightStrengthSlider.width * 30.0f), 0.0f, 30.0f) / 10.0f;
                    float step = GetFrameTime();
                    float dx = (IsKeyDown(KEY_RIGHT) ? step : 0) - (IsKeyDown(KEY_LEFT) ? step : 0);
                    float dz = (IsKeyDown(KEY_DOWN) ? step : 0) - (IsKeyDown(KEY_UP) ? step : 0);
                    float dy = (IsKeyDown(KEY_E) ? step : 0) - (IsKeyDown(KEY_Q) ? step : 0);
                    Vector2 drag = {};
                    if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT) && CheckCollisionPointRec(mouse, viewport) && !(showColorPicker && CheckCollisionPointRec(mouse, colorPanel)) && !(showPlanePanel && CheckCollisionPointRec(mouse, planePanel)) && !(showPhysicsPanel && CheckCollisionPointRec(mouse, physicsPanel))) drag = GetMouseDelta();
                    float change = tool == Tool::Select ? 0.0f : (IsKeyDown(KEY_EQUAL) || IsKeyDown(KEY_KP_ADD) ? step : 0.0f) -
                                   (IsKeyDown(KEY_MINUS) || IsKeyDown(KEY_KP_SUBTRACT) ? step : 0.0f);
                    Vector3 center{};
                    for (int index : selection) center = Vector3Add(center, parts[index].position);
                    if (!selection.empty()) center = Vector3Scale(center, 1.0f / selection.size());
                    for (int index : selection)
                    {
                    Part& item = parts[index];
                    if (tool == Tool::Move)
                    {
                        item.position.x += dx + drag.x * 0.01f;
                        item.position.z += dz;
                        item.position.y += dy + (IsKeyDown(KEY_U) ? step : 0.0f) - (IsKeyDown(KEY_J) ? step : 0.0f) - drag.y * 0.01f;
                    }
                    else if (tool == Tool::Rotate)
                    {
                        float ry = dx * 90 + drag.x * 0.7f, rx = dz * 90 + drag.y * 0.7f, rz = dy * 90;
                        item.rotation.y += ry; item.rotation.x += rx; item.rotation.z += rz;
                        Vector3 offset = Vector3Subtract(item.position, center);
                        offset = Vector3RotateByAxisAngle(offset, {1, 0, 0}, rx * DEG2RAD);
                        offset = Vector3RotateByAxisAngle(offset, {0, 1, 0}, ry * DEG2RAD);
                        offset = Vector3RotateByAxisAngle(offset, {0, 0, 1}, rz * DEG2RAD);
                        item.position = Vector3Add(center, offset);
                    }
                    else if (tool == Tool::Scale)
                    {
                        float sx = dx + drag.x * 0.01f, sy = dy - drag.y * 0.01f, sz = dz;
                        item.size.x = std::clamp(item.size.x + sx, 0.04f, 20.0f);
                        item.size.y = std::clamp(item.size.y + sy, 0.04f, 20.0f);
                        item.size.z = std::clamp(item.size.z + sz, 0.04f, 20.0f);
                        item.position.x = center.x + (item.position.x - center.x) * std::max(0.01f, 1 + sx);
                        item.position.y = center.y + (item.position.y - center.y) * std::max(0.01f, 1 + sy);
                        item.position.z = center.z + (item.position.z - center.z) * std::max(0.01f, 1 + sz);
                    }
                    else if (tool == Tool::Extrude)
                    {
                        float amount = dy - dz - drag.y * 0.01f;
                        if (amount != 0)
                        {
                            bool flatPlane = item.type == 4 && fabsf(item.bend) < 0.001f;
                            if (item.type != 0 && item.type != 3 && !flatPlane)
                            {
                                status = "Extrusion por cara: cubo, cilindro o plano recto";
                                continue;
                            }
                            int targetFace = item.type == 0 ? face : (face == 3 ? 3 : 2);
                            int dimension = targetFace / 2;
                            float sign = targetFace % 2 == 0 ? 1.0f : -1.0f;
                            float* axisSize = dimension == 0 ? &item.size.x : dimension == 1 ? &item.size.y : &item.size.z;
                            float previous = flatPlane ? item.thickness * item.size.y : *axisSize;
                            float next = flatPlane ? std::clamp(previous + amount, 0.001f * item.size.y, 0.5f * item.size.y) : std::clamp(previous + amount, 0.04f, 20.0f);
                            if (flatPlane) item.thickness = next / item.size.y;
                            else *axisSize = next;
                            Matrix rot = MatrixMultiply(MatrixRotateZ(item.rotation.z * DEG2RAD), MatrixMultiply(MatrixRotateX(item.rotation.x * DEG2RAD), MatrixRotateY(item.rotation.y * DEG2RAD)));
                            Vector3 localAxis = dimension == 0 ? Vector3{item.mirrorX ? -1.0f : 1.0f, 0, 0} : dimension == 1 ? Vector3{0, item.mirrorY ? -1.0f : 1.0f, 0} : Vector3{0, 0, item.mirrorZ ? -1.0f : 1.0f};
                            Vector3 axis = Vector3Transform(localAxis, rot);
                            item.position = Vector3Add(item.position, Vector3Scale(axis, sign * (next - previous) * 0.5f));
                        }
                    }
                    item.size.x = std::clamp(item.size.x + change, 0.04f, 20.0f);
                    item.size.y = std::clamp(item.size.y + change, 0.04f, 20.0f);
                    item.size.z = std::clamp(item.size.z + change, 0.04f, 20.0f);
                    if (change != 0) item.position = Vector3Add(center, Vector3Scale(Vector3Subtract(item.position, center), std::max(0.01f, 1 + change)));
                    }
                }
            }
            bool continuous = (!control && (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_UP) || IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_Q) || IsKeyDown(KEY_E) || IsKeyDown(KEY_U) || IsKeyDown(KEY_J) || IsKeyDown(KEY_EQUAL) || IsKeyDown(KEY_MINUS) || IsKeyDown(KEY_KP_ADD) || IsKeyDown(KEY_KP_SUBTRACT))) ||
                              IsMouseButtonDown(MOUSE_BUTTON_RIGHT) || (showColorPicker && IsMouseButtonDown(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, colorPanel)) || (showPlanePanel && IsMouseButtonDown(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, planePanel)) || (showPhysicsPanel && IsMouseButtonDown(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, physicsPanel) && !CheckCollisionPointRec(mouse, fixedButton)) || (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, lightStrengthSlider));
            bool changed = !SameParts(before.parts, parts);
            if (!historyAction && !resetHistory && changed && HasNewCollision(before.parts, parts))
            {
                parts = before.parts;
                selection = before.selection;
                selectedPart = before.active;
                status = "Movimiento bloqueado por cuerpo rigido";
                changed = false;
            }
            if (resetHistory) { undoHistory.clear(); redoHistory.clear(); }
            else if (!historyAction && changed)
            {
                if (!continuous || !continuousPrevious) { undoHistory.push_back(std::move(before)); if (undoHistory.size() > 100) undoHistory.erase(undoHistory.begin()); }
                redoHistory.clear();
            }
            continuousPrevious = continuous && !historyAction && !resetHistory && (continuousPrevious || changed);
        }
        if (selectedPart < 0 || selectedPart >= static_cast<int>(parts.size()) || parts[selectedPart].type != 4) showPlanePanel = false;
        if (selectedPart < 0 || selectedPart >= static_cast<int>(parts.size()) || !parts[selectedPart].rigid) showPhysicsPanel = false;
        bool colorInteraction = editing && ((showColorPicker && CheckCollisionPointRec(mouse, colorPanel)) || (showPlanePanel && CheckCollisionPointRec(mouse, planePanel)) || (showPhysicsPanel && CheckCollisionPointRec(mouse, physicsPanel)));
        bool viewportClick = CheckCollisionPointRec(mouse, viewport) && !menuInteraction && !colorInteraction &&
                             !(editing && CheckCollisionPointRec(mouse, actionArea)) &&
                             (editing || (!CheckCollisionPointRec(mouse, left) && !CheckCollisionPointRec(mouse, right)));
        bool snappedView = false;
        if (clicked)
        {
            double now = GetTime();
            if (viewportClick && lastViewportClick >= 0 && now - lastViewportClick <= 0.35 && Vector2Distance(mouse, lastViewportClickPosition) <= 12.0f)
            {
                SnapCameraToNearestSide(yaw, pitch);
                lastViewportClick = -1;
                snappedView = true;
            }
            else if (viewportClick) { lastViewportClick = now; lastViewportClickPosition = mouse; }
            else lastViewportClick = -1;
        }
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && !snappedView && !selectionClicked && !menuInteraction && !colorInteraction && !(editing && CheckCollisionPointRec(mouse, actionArea)) && CheckCollisionPointRec(mouse, viewport) &&
            (editing || (!CheckCollisionPointRec(mouse, left) && !CheckCollisionPointRec(mouse, right))))
        {
            Vector2 delta = GetMouseDelta();
            if (delta.x != 0 || delta.y != 0)
            {
                yaw -= delta.x * 0.01f;
                pitch = std::clamp(pitch + delta.y * 0.01f, -1.56f, 1.56f);
            }
        }
        if (!menuInteraction && !colorInteraction && CheckCollisionPointRec(mouse, viewport)) distance = std::clamp(distance - GetMouseWheelMove() * 0.35f, 1.0f, 10.0f);
        if (IsKeyDown(KEY_I)) camera.target.y += GetFrameTime();
        if (IsKeyDown(KEY_K)) camera.target.y -= GetFrameTime();
        camera.target.y = std::clamp(camera.target.y, -10.0f, 10.0f);
        camera.position = {camera.target.x + distance * cosf(pitch) * sinf(yaw), camera.target.y + distance * sinf(pitch), camera.target.z + distance * cosf(pitch) * cosf(yaw)};
        camera.up = fabsf(cosf(pitch)) < 0.0001f ? Vector3{0, 0, pitch > 0 ? -1.0f : 1.0f} : Vector3{0, 1, 0};

        if (physicsRunning) StepPhysics(previewParts, previewVelocity, GetFrameTime());
        const std::vector<Part>* activeParts = editing ? (physicsRunning ? &previewParts : &parts) : (showingSaved ? &catalogParts : nullptr);
        Vector3 emitterPositions[8]{};
        Vector3 emitterColors[8]{};
        float emitterStrengths[8]{};
        int emitterOccluders[8]{};
        Vector3 occluderMin[32]{}, occluderMax[32]{};
        int occluderTypes[32]{};
        Matrix occluderInverse[32]{};
        int occluderCount = 0;
        std::vector<int> occluderForPart(activeParts ? activeParts->size() : 0, -1);
        if (activeParts)
            for (size_t i = 0; i < activeParts->size() && occluderCount < 32; ++i)
            {
                const Part& part = (*activeParts)[i];
                if (part.emissive && part.lightHidden) continue;
                BoundingBox box = PartBounds(part);
                occluderMin[occluderCount] = box.min;
                occluderMax[occluderCount] = box.max;
                occluderTypes[occluderCount] = part.type == 1 ? 1 : part.type == 3 ? 2 : part.type == 2 ? 3 : part.type == 13 ? 4 : 0;
                if (occluderTypes[occluderCount] >= 2) occluderInverse[occluderCount] = PartInverseTransform(part);
                occluderForPart[i] = occluderCount++;
            }
        int emitterCount = 0;
        if (activeParts)
            for (size_t i = 0; i < activeParts->size(); ++i)
            {
                const Part& part = (*activeParts)[i];
                if (part.emissive && emitterCount < 8)
                {
                    emitterPositions[emitterCount] = part.position;
                    Color c = part.color;
                    emitterColors[emitterCount] = {c.r / 255.0f, c.g / 255.0f, c.b / 255.0f};
                    emitterStrengths[emitterCount] = part.lightStrength;
                    emitterOccluders[emitterCount] = occluderForPart[i];
                    ++emitterCount;
                }
            }
        BeginTextureMode(scene);
        ClearBackground({0, 0, 0, 0});
        BeginMode3D(camera);
        SetShaderValue(lighting, lightLocation, &lightPosition, SHADER_UNIFORM_VEC3);
        SetShaderValue(lighting, countLocation, &emitterCount, SHADER_UNIFORM_INT);
        SetShaderValue(lighting, boundsCountLocation, &occluderCount, SHADER_UNIFORM_INT);
        if (occluderCount > 0)
        {
            SetShaderValueV(lighting, boundsMinLocation, occluderMin, SHADER_UNIFORM_VEC3, occluderCount);
            SetShaderValueV(lighting, boundsMaxLocation, occluderMax, SHADER_UNIFORM_VEC3, occluderCount);
            SetShaderValueV(lighting, boundsTypeLocation, occluderTypes, SHADER_UNIFORM_INT, occluderCount);
            for (int i = 0; i < occluderCount; ++i)
                if (occluderTypes[i] >= 2) SetShaderValueMatrix(lighting, inverseLocations[i], occluderInverse[i]);
        }
        if (emitterCount > 0)
        {
            SetShaderValueV(lighting, positionsLocation, emitterPositions, SHADER_UNIFORM_VEC3, emitterCount);
            SetShaderValueV(lighting, colorsLocation, emitterColors, SHADER_UNIFORM_VEC3, emitterCount);
            SetShaderValueV(lighting, strengthsLocation, emitterStrengths, SHADER_UNIFORM_FLOAT, emitterCount);
            SetShaderValueV(lighting, emitterOccludersLocation, emitterOccluders, SHADER_UNIFORM_INT, emitterCount);
        }
        BeginShaderMode(lighting);
        rlDrawRenderBatchActive();
        rlDisableColorBlend(); // Conservar la intensidad exacta en el canal alfa.
        if (activeParts)
        {
            for (size_t partIndex = 0; partIndex < activeParts->size(); ++partIndex)
            {
                const Part& part = (*activeParts)[partIndex];
                if (part.emissive && part.lightHidden) continue;
                int emission = part.emissive && part.lightStrength > 0 ? 1 : 0;
                int receiver = occluderForPart[partIndex];
                SetShaderValue(lighting, selfLocation, &emission, SHADER_UNIFORM_INT);
                SetShaderValue(lighting, receiverLocation, &receiver, SHADER_UNIFORM_INT);
                DrawPart(part);
                rlDrawRenderBatchActive();
            }
        }
        else
        {
            int emission = 0;
            int receiver = -1;
            SetShaderValue(lighting, selfLocation, &emission, SHADER_UNIFORM_INT);
            SetShaderValue(lighting, receiverLocation, &receiver, SHADER_UNIFORM_INT);
            DrawModel(selected);
        }
        rlDrawRenderBatchActive();
        rlEnableColorBlend();
        EndShaderMode();
        EndMode3D();
        EndTextureMode();

        Color* maskPixels = nullptr;
        if (activeParts && emitterCount > 0)
        {
            BeginTextureMode(mask);
            ClearBackground(BLACK);
            BeginMode3D(camera);
            BeginShaderMode(maskShader);
            rlDrawRenderBatchActive();
            rlDisableColorBlend();
            for (const Part& part : *activeParts)
            {
                if (part.emissive && part.lightHidden) continue;
                int emission = part.emissive && part.lightStrength > 0 ? 1 : 0;
                SetShaderValue(maskShader, maskEmitterLocation, &emission, SHADER_UNIFORM_INT);
                DrawPart(part);
                rlDrawRenderBatchActive();
            }
            rlEnableColorBlend();
            EndShaderMode();
            EndMode3D();
            EndTextureMode();
            Image maskImage = LoadImageFromTexture(mask.texture);
            ImageFlipVertical(&maskImage);
            maskPixels = LoadImageColors(maskImage);
            UnloadImage(maskImage);
        }
        Image image = LoadImageFromTexture(scene.texture);
        ImageFlipVertical(&image);
        Color* pixels = LoadImageColors(image);
        BeginDrawing();
        ClearBackground({8, 9, 9, 255});
        DrawRectangle(0, 0, width, 44, {24, 27, 28, 255});
        DrawRectangle(0, 44, width, 126, {32, 35, 36, 255});
        DrawText("ASCII FORGE", 20, 13, 18, gold);
        DrawText(editing ? (tool == Tool::Select ? "SELECCION" : "TALLER") : "CATALOGO", 165, 15, 14, muted);
        DrawPixelButton(modeButton, {52, 68, 77, 255});
        DrawText(editing ? "M: CATALOGO" : "M: CREAR MODELO", modeButton.x + 8, modeButton.y + 10, 13, gold);
        DrawPixelButton(lightButton, movingLight ? Color{117, 91, 47, 255} : Color{70, 62, 43, 255});
        DrawText(movingLight ? "L: LUZ ACTIVA" : "L: MOVER LUZ", lightButton.x + 6, lightButton.y + 10, 12, gold);
        if (!editing)
        {
            DrawPixelButton(savedButton, {57, 69, 62, 255});
            DrawText("MIS MODELOS v", savedButton.x + 8, savedButton.y + 10, 12, gold);
            DrawPixelButton(deleteOneButton, {86, 43, 40, 255}, !showingSaved);
            DrawPixelButton(deleteAllButton, {86, 43, 40, 255});
            DrawPixelButton(duplicateButton, {48, 83, 66, 255});
            DrawText("ELIMINAR MODELO", deleteOneButton.x + 7, deleteOneButton.y + 9, 10, showingSaved ? gold : muted);
            DrawText("VACIAR GUARDADOS", deleteAllButton.x + 7, deleteAllButton.y + 9, 10, gold);
            DrawText("+ DUPLICAR MODELO", duplicateButton.x + 6, duplicateButton.y + 9, 10, gold);
            if (!showingSaved) DrawText("ORIGINAL INMUTABLE", 185, 88, 10, muted);
        }
        DrawText(TextFormat("TEX %.1f", textSize / 10.0f), width - 265, 53, 11, gold);
        DrawText(TextFormat("DEN %.1f", density / 10.0f), width - 265, 83, 11, gold);
        DrawRectangle(textSlider.x, textSlider.y + 8, textSlider.width, 3, muted);
        DrawCircle(static_cast<int>(textSlider.x + textSlider.width * (textSize - 5) / 75.0f), textSlider.y + 9, 5, gold);
        DrawRectangle(densitySlider.x, densitySlider.y + 8, densitySlider.width, 3, muted);
        DrawCircle(static_cast<int>(densitySlider.x + densitySlider.width * (density - 5) / 45.0f), densitySlider.y + 9, 5, gold);
        DrawLine(0, 170, width, 170, muted);
        DrawRectangleRec(viewport, {2, 3, 3, 255});
        DrawRectangleLinesEx(viewport, 1, muted);
        if (!editing) DrawText("INSPECCION", viewport.x + 18, viewport.y + 15, 12, muted);

        float cellHeight = viewport.height / rows;
        float cellWidth = viewport.width / columns;
        float fontSize = viewport.height / 30.0f * (textSize / 10.0f);
        float startX = viewport.x;
        float startY = viewport.y;
        BeginScissorMode(viewport.x + 1, viewport.y + 1, viewport.width - 2, viewport.height - 2);
        if (pixels)
        {
            // Aura ASCII basada en las superficies emisoras visibles.
            bool torchAura = !editing && !showingSaved && selected == 2;
            if (maskPixels || torchAura)
            {
                float flicker = 0.8f + 0.2f * sinf(static_cast<float>(GetTime()) * 5.0f);
                for (int y = 0; y < rows; ++y)
                    for (int x = 0; x < columns; ++x)
                    {
                        if (pixels[y * columns + x].a != 0) continue;
                        float nearest = 4.0f;
                        Color auraColor = {255, 145, 55, 255};
                        for (int dy = -3; dy <= 3; ++dy)
                            for (int dx = -3; dx <= 3; ++dx)
                            {
                                int px = x + dx, py = y + dy;
                                if (px < 0 || px >= columns || py < 0 || py >= rows) continue;
                                Color p = maskPixels ? maskPixels[py * columns + px] : pixels[py * columns + px];
                                bool emitter = maskPixels ? (p.r != 0 || p.g != 0 || p.b != 0) :
                                    (p.a && p.r > 220 && p.r > p.g + 25 && p.g > p.b + 25);
                                float d = sqrtf(static_cast<float>(dx * dx + dy * dy));
                                if (emitter && d < nearest) { nearest = d; if (maskPixels) auraColor = p; }
                            }
                        if (nearest < 3.5f)
                        {
                            float strength = (1.0f - nearest / 3.5f) * flicker;
                            Color glow = {auraColor.r, auraColor.g, auraColor.b, static_cast<unsigned char>(strength * 150)};
                            DrawTextEx(asciiFont, nearest < 1.8f ? ";" : ".", {startX + x * cellWidth, startY + y * cellHeight}, fontSize, 0, glow);
                        }
                    }
            }
            for (int y = 0; y < rows; ++y)
                for (int x = 0; x < columns; ++x)
                {
                    Color pixel = pixels[y * columns + x];
                    int brightness = pixel.a;
                    if (brightness > 0)
                    {
                        int index = std::min(8, 11 - std::min(11, brightness * 12 / 256));
                        float shade = 0.55f + 0.45f * brightness / 255.0f;
                        Color tint = {static_cast<unsigned char>(pixel.r * shade), static_cast<unsigned char>(pixel.g * shade), static_cast<unsigned char>(pixel.b * shade), 255};
                        DrawTextEx(asciiFont, symbols[index], {startX + x * cellWidth, startY + y * cellHeight}, fontSize, 0, tint);
                    }
                }
        }
        EndScissorMode();
        DrawRectangleLinesEx(viewport, 1, muted);
        if (editing)
        {
            BeginScissorMode(viewport.x + 1, viewport.y + 1, viewport.width - 2, viewport.height - 2);
            for (int i = 0; !physicsRunning && i < static_cast<int>(parts.size()); ++i)
            {
                Vector2 projected = GetWorldToScreenEx(parts[i].position, camera, columns, rows);
                bool chosen = std::find(selection.begin(), selection.end(), i) != selection.end();
                if (chosen && parts[i].rigid)
                {
                    BoundingBox box = PartBounds(parts[i]);
                    Vector2 corners[8];
                    bool inFront = true;
                    for (int j = 0; j < 8; ++j)
                    {
                        Vector3 point = {j & 1 ? box.max.x : box.min.x, j & 2 ? box.max.y : box.min.y, j & 4 ? box.max.z : box.min.z};
                        if (Vector3DotProduct(Vector3Subtract(point, camera.position), Vector3Subtract(camera.target, camera.position)) <= 0.01f) inFront = false;
                        Vector2 p = GetWorldToScreenEx(point, camera, columns, rows);
                        corners[j] = {startX + p.x * cellWidth, startY + p.y * cellHeight};
                    }
                    if (inFront)
                        for (int j = 0; j < 8; ++j)
                            for (int axis = 0; axis < 3; ++axis)
                                if (!(j & (1 << axis))) DrawLineEx(corners[j], corners[j | (1 << axis)], 1, {100, 210, 190, 255});
                }
                if (projected.x >= 0 && projected.x < columns && projected.y >= 0 && projected.y < rows)
                {
                    int px = static_cast<int>(startX + projected.x * cellWidth), py = static_cast<int>(startY + projected.y * cellHeight);
                    DrawCircleLines(px, py, chosen ? 6 : 4, chosen && parts[i].rigid ? Color{100, 210, 190, 255} : chosen ? Color{235, 185, 100, 255} : muted);
                    if (chosen) DrawText("+", px - 4, py - 7, 14, {235, 185, 100, 255});
                }
            }
            EndScissorMode();
            if (!physicsRunning && tool == Tool::Extrude && selectedPart >= 0 && selectedPart < static_cast<int>(parts.size()))
            {
                const Part& p = parts[selectedPart];
                bool flatPlane = p.type == 4 && fabsf(p.bend) < 0.001f;
                if (p.type == 0 || p.type == 3 || flatPlane)
                {
                    int f = p.type == 0 ? face : (face == 3 ? 3 : 2);
                    int axis = f / 2;
                    float sign = f % 2 == 0 ? 1.0f : -1.0f;
                    Vector3 half = {p.size.x * 0.5f, p.size.y * (flatPlane ? p.thickness : 1.0f) * 0.5f, p.size.z * 0.5f};
                    Matrix rot = MatrixMultiply(MatrixRotateZ(p.rotation.z * DEG2RAD), MatrixMultiply(MatrixRotateX(p.rotation.x * DEG2RAD), MatrixRotateY(p.rotation.y * DEG2RAD)));
                    Vector2 points[4];
                    const float u[] = {-1, -1, 1, 1}, v[] = {-1, 1, 1, -1};
                    for (int i = 0; i < 4; ++i)
                    {
                        Vector3 local = axis == 0 ? Vector3{sign * half.x, u[i] * half.y, v[i] * half.z} :
                                        axis == 1 ? Vector3{u[i] * half.x, sign * half.y, v[i] * half.z} :
                                                    Vector3{u[i] * half.x, v[i] * half.y, sign * half.z};
                        if (p.mirrorX) local.x = -local.x;
                        if (p.mirrorY) local.y = -local.y;
                        if (p.mirrorZ) local.z = -local.z;
                        Vector3 world = Vector3Add(p.position, Vector3Transform(local, rot));
                        Vector2 projected = GetWorldToScreenEx(world, camera, columns, rows);
                        points[i] = {startX + projected.x * cellWidth, startY + projected.y * cellHeight};
                    }
                    BeginScissorMode(viewport.x + 1, viewport.y + 1, viewport.width - 2, viewport.height - 2);
                    for (int i = 0; i < 4; ++i) DrawLineEx(points[i], points[(i + 1) % 4], 2, {110, 220, 210, 255});
                    EndScissorMode();
                }
            }
            const char* editLabels[] = {"DESHACER", "REHACER", "COPIAR", "CORTAR", "PEGAR", "TODOS"};
            for (int i = 0; i < 6; ++i)
            {
                DrawPixelButton(editActions[i], i == 3 ? Color{86, 43, 40, 255} : Color{46, 57, 64, 255});
                DrawText(editLabels[i], editActions[i].x + 3, editActions[i].y + 8, 9, gold);
            }
            DrawPixelButton(donutButton, {49, 68, 56, 255});
            DrawPixelButton(faceButton, {46, 73, 76, 255});
            DrawPixelButton(planeButton, {68, 58, 79, 255});
            DrawText("6 DONA", donutButton.x + 5, donutButton.y + 8, 10, gold);
            DrawPixelButton(physicsButton, {65, 61, 84, 255}, physicsRunning);
            DrawText("FISICA...", physicsButton.x + 12, physicsButton.y + 8, 10, physicsRunning ? muted : gold);
            DrawPixelButton(previewButton, physicsRunning ? Color{104, 73, 44, 255} : Color{48, 83, 66, 255});
            DrawText(physicsRunning ? "T: DETENER" : "T: SIMULAR", previewButton.x + 10, previewButton.y + 8, 10, gold);
            for (int i = 0; i < 3; ++i)
            {
                bool reflected = selectedPart >= 0 && (i == 0 ? parts[selectedPart].mirrorX : i == 1 ? parts[selectedPart].mirrorY : parts[selectedPart].mirrorZ);
                DrawPixelButton(mirrorButtons[i], reflected ? Color{99, 76, 113, 255} : Color{62, 52, 70, 255}, selection.empty());
                DrawText(i == 0 ? "REFLEJAR X" : i == 1 ? "REFLEJAR Y" : "REFLEJAR Z", mirrorButtons[i].x + 6, mirrorButtons[i].y + 9, 10, selection.empty() ? muted : gold);
            }
            bool canAdjustLight = selectedPart >= 0 && parts[selectedPart].emissive && !movingLight;
            bool hiddenLight = canAdjustLight && parts[selectedPart].lightHidden;
            DrawPixelButton(hideLightButton, hiddenLight ? Color{100, 78, 44, 255} : Color{62, 57, 43, 255}, !canAdjustLight);
            DrawText(hiddenLight ? "H: MOSTRAR" : "H: OCULTAR", hideLightButton.x + 8, hideLightButton.y + 9, 10, canAdjustLight ? gold : muted);
            if (canAdjustLight)
            {
                float intensity = parts[selectedPart].lightStrength;
                DrawText(TextFormat("LUZ %.1f", intensity), 455, 115, 11, gold);
                DrawRectangle(lightStrengthSlider.x, lightStrengthSlider.y + 8, lightStrengthSlider.width, 3, muted);
                DrawCircle(static_cast<int>(lightStrengthSlider.x + lightStrengthSlider.width * intensity / 3.0f), lightStrengthSlider.y + 9, 5, gold);
            }
            const char* faceLabels[] = {"F X+", "F X-", "F Y+", "F Y-", "F Z+", "F Z-"};
            int shownFace = selectedPart >= 0 && (parts[selectedPart].type == 3 || parts[selectedPart].type == 4) && face != 3 ? 2 : face;
            DrawText(faceLabels[shownFace], faceButton.x + 5, faceButton.y + 8, 10, gold);
            DrawText("P PLANO", planeButton.x + 5, planeButton.y + 8, 10, gold);
            const char* primitiveLabels[] = {"1 CUBO", "2 ESFERA", "3 CONO", "4 CILIN.", "5 PLANO"};
            for (int i = 0; i < 4; ++i)
            {
                DrawPixelButton(tools[i], static_cast<int>(tool) == i ? Color{83, 101, 103, 255} : Color{45, 58, 65, 255});
                Color ink = gold;
                int ix = static_cast<int>(tools[i].x + 10), iy = static_cast<int>(tools[i].y + 15);
                if (i == 0) { DrawLine(ix - 5, iy, ix + 5, iy, ink); DrawLine(ix, iy - 5, ix, iy + 5, ink); }
                if (i == 1) DrawCircleLines(ix, iy, 5, ink);
                if (i == 2) DrawRectangleLines(ix - 5, iy - 5, 10, 10, ink);
                if (i == 3) { DrawLine(ix - 4, iy + 4, ix + 4, iy + 4, ink); DrawLine(ix, iy + 4, ix, iy - 5, ink); DrawLine(ix, iy - 5, ix + 3, iy - 2, ink); }
                DrawText(toolNames[i], tools[i].x + 20, tools[i].y + 9, 10, ink);
            }
            for (int i = 0; i < 5; ++i)
            {
                DrawPixelButton(primitives[i], {49, 68, 56, 255});
                int ix = static_cast<int>(primitives[i].x + 9), iy = static_cast<int>(primitives[i].y + 15);
                if (i == 0) DrawRectangleLines(ix - 5, iy - 5, 10, 10, gold);
                if (i == 1) DrawCircleLines(ix, iy, 5, gold);
                if (i == 2) { DrawLine(ix, iy - 6, ix - 5, iy + 5, gold); DrawLine(ix, iy - 6, ix + 5, iy + 5, gold); DrawLine(ix - 5, iy + 5, ix + 5, iy + 5, gold); }
                if (i == 3) { DrawRectangleLines(ix - 4, iy - 5, 8, 10, gold); DrawLine(ix - 4, iy - 3, ix + 4, iy - 3, gold); }
                if (i == 4) { DrawLine(ix - 6, iy + 3, ix + 5, iy + 3, gold); DrawLine(ix - 4, iy - 2, ix + 6, iy - 2, gold); }
                DrawText(primitiveLabels[i], primitives[i].x + 18, primitives[i].y + 10, 10, gold);
            }
            for (Rectangle button : {saveButton, loadButton, colorButton})
            {
                DrawPixelButton(button, {55, 62, 76, 255});
            }
            DrawText("[S] GUARDAR", saveButton.x + 5, saveButton.y + 10, 10, gold);
            DrawText("[O] ABRIR", loadButton.x + 5, loadButton.y + 10, 10, gold);
            DrawText("[C] COLOR", colorButton.x + 5, colorButton.y + 10, 10, gold);
        }
        else
        {
        DrawRectangleLinesEx(left, 1, gold);
        DrawRectangleLinesEx(right, 1, gold);
        DrawText("<", left.x + 16, left.y + 8, 20, gold);
        DrawText(">", right.x + 16, right.y + 8, 20, gold);
        const char* counter = showingSaved && viewingExample < 0 ? "MODELO GUARDADO" : TextFormat("%02i  /  %02i", selected + 1, modelCount);
        DrawText(counter, width / 2 - MeasureText(counter, 14) / 2, viewport.y + viewport.height - 43, 14, muted);
        }
        if (editing)
        {
            const char* info = selectedPart >= 0 ? TextFormat("%i SELEC. / %i : %s", static_cast<int>(selection.size()), static_cast<int>(parts.size()), parts[selectedPart].type < 5 ? partNames[parts[selectedPart].type] : parts[selectedPart].type == 13 ? "DONA" : names[parts[selectedPart].type - 5]) : "SIN SELECCION";
            DrawText(physicsRunning ? "VISTA PREVIA DE FISICA" : info, 40, height - 118, 20, gold);
            bool active = selectedPart >= 0 && selectedPart < static_cast<int>(parts.size()) && parts[selectedPart].emissive;
            bool isRigid = selectedPart >= 0 && selectedPart < static_cast<int>(parts.size()) && parts[selectedPart].rigid;
            DrawPixelButton(rigidButton, isRigid ? Color{62, 95, 89, 255} : Color{45, 55, 54, 255}, selectedPart < 0);
            DrawText(isRigid ? "R: RIGIDO SI" : "R: RIGIDO NO", rigidButton.x + 8, rigidButton.y + 10, 11, selectedPart < 0 ? muted : gold);
            DrawPixelButton(emissionButton, active ? Color{112, 85, 40, 255} : Color{62, 57, 43, 255});
            DrawText(active ? "B: LUZ ACTIVADA" : "B: HACER LUZ", emissionButton.x + 8, emissionButton.y + 10, 11, gold);
            DrawText(status.empty() ? "TAB: SELECCIONAR  |  SUPR: BORRAR  |  F1-F4: MODOS  |  CTRL+S: GUARDAR  CTRL+O: ABRIR" : status.c_str(), 40, height - 87, 13, muted);
        }
        else
        {
            DrawText(showingSaved && viewingExample < 0 ? catalogFile.c_str() : names[selected], 40, height - 118, 22, gold);
            DrawPixelButton(editButton, {48, 83, 66, 255});
            DrawText("EDITAR EN TALLER", editButton.x + 10, editButton.y + 10, 11, gold);
            DrawText(GetTime() < confirmEditUntil ? catalogStatus.c_str() : (GetTime() < noticeUntil ? catalogNotice.c_str() : (showingSaved ? "Version editada guardada en tu taller." : descriptions[selected])), 40, height - 87, 15, muted);
        }
        DrawLine(35, height - 55, width - 35, height - 55, muted);
        const char* controls = movingLight ? TextFormat("LUZ X %.1f  Y %.1f  Z %.1f  |  FLECHAS: MOVER X/Z   Q/E: ALTURA   L: TERMINAR", lightPosition.x, lightPosition.y, lightPosition.z) :
            (editing ? (tool == Tool::Select ? "SELECCION: CLIC EN UNA PIEZA  |  MAYUS+CLIC: VARIAS  |  ELIGE UNA HERRAMIENTA" : "FLECHAS Y Q/E: TRANSFORMAR   |   U/J: ALTURA   |   RATON DERECHO: EDITAR   +/-: ESCALA") : "FLECHAS: MOVER VISTA   |   CLIC EN < >: CAMBIAR MODELO   |   RUEDA: ZOOM");
        BeginScissorMode(35, height - 48, std::max(0, width - 160), 38);
        DrawText(controls, 40, height - 37, 12, muted);
        EndScissorMode();
        DrawPixelButton(exitButton, {78, 43, 40, 255});
        DrawText("SALIR", exitButton.x + 25, exitButton.y + 9, 12, gold);
        if (editing && showPhysicsPanel && !physicsRunning && selectedPart >= 0 && parts[selectedPart].rigid)
        {
            const Part& p = parts[selectedPart];
            const char* labels[] = {"REBOTE", "DUREZA", "MASA", "AIRE", "FRICCION"};
            float values[] = {p.bounce / 1.5f, p.hardness / 2.0f, (log10f(p.mass) + 3.0f) / 6.0f, p.airDrag / 2.0f, p.friction};
            DrawRectangleRec(physicsPanel, {24, 27, 28, 255});
            DrawRectangleLinesEx(physicsPanel, 1, muted);
            DrawText("PROPIEDADES FISICAS", physicsPanel.x + 10, physicsPanel.y + 10, 11, gold);
            for (int i = 0; i < 5; ++i)
            {
                Rectangle slider = {physicsPanel.x + 90, physicsPanel.y + 35 + i * 30.0f, 110, 20};
                DrawText(labels[i], physicsPanel.x + 10, slider.y + 5, 10, gold);
                DrawRectangle(slider.x, slider.y + 9, slider.width, 3, muted);
                DrawCircle(slider.x + slider.width * values[i], slider.y + 10, 5, gold);
                float actual = i == 0 ? p.bounce : i == 1 ? p.hardness : i == 2 ? p.mass : i == 3 ? p.airDrag : p.friction;
                DrawText(i == 2 ? TextFormat("%.3g", actual) : TextFormat("%.2f", actual), physicsPanel.x + 203, slider.y + 5, 10, gold);
            }
            DrawPixelButton(fixedButton, p.physicsFixed ? Color{85, 74, 47, 255} : Color{45, 58, 65, 255});
            DrawText(p.physicsFixed ? "FIJO: SI (NO CAE)" : "FIJO: NO (MOVIL)", fixedButton.x + 8, fixedButton.y + 8, 10, gold);
        }
        if (editing && showPlanePanel && selectedPart >= 0 && parts[selectedPart].type == 4)
        {
            const Part& p = parts[selectedPart];
            const char* labels[] = {"CURVA", "LARGO", "ANCHO", "GROSOR"};
            float values[] = {(p.bend + 360) / 720, (p.size.x - 0.04f) / 4.96f, (p.size.z - 0.04f) / 4.96f, (p.thickness - 0.001f) / 0.499f};
            DrawRectangleRec(planePanel, {24, 27, 28, 255});
            DrawRectangleLinesEx(planePanel, 1, muted);
            DrawText("FORMA DEL PLANO / P: CERRAR", planePanel.x + 10, planePanel.y + 8, 10, gold);
            for (int i = 0; i < 4; ++i)
            {
                Rectangle slider = {planePanel.x + 73, planePanel.y + 28 + i * 32.0f, 130, 22};
                DrawText(labels[i], planePanel.x + 8, slider.y + 6, 10, gold);
                DrawRectangle(slider.x, slider.y + 10, slider.width, 2, muted);
                DrawCircle(slider.x + slider.width * std::clamp(values[i], 0.0f, 1.0f), slider.y + 11, 5, gold);
                const char* value = i == 0 ? TextFormat("%.0f", p.bend) : i == 1 ? TextFormat("%.2f", p.size.x) : i == 2 ? TextFormat("%.2f", p.size.z) : TextFormat("%.3f", p.thickness);
                DrawText(value, planePanel.x + 207, slider.y + 6, 10, gold);
            }
        }
        if (editing && showColorPicker && selectedPart >= 0 && selectedPart < static_cast<int>(parts.size()))
        {
            Color color = parts[selectedPart].color;
            const int values[] = {color.r, color.g, color.b};
            const char* labels[] = {"R", "G", "B"};
            const Color channels[] = {{225, 85, 85, 255}, {85, 210, 110, 255}, {100, 150, 240, 255}};
            DrawRectangleRec(colorPanel, {24, 27, 28, 255});
            DrawRectangleLinesEx(colorPanel, 1, muted);
            DrawText("COLOR RGB  /  C: CERRAR", colorPanel.x + 10, colorPanel.y + 10, 11, gold);
            for (int channel = 0; channel < 3; ++channel)
            {
                Rectangle slider = {colorPanel.x + 35, colorPanel.y + 32 + channel * 30.0f, 150, 20};
                DrawText(labels[channel], colorPanel.x + 12, slider.y + 4, 12, channels[channel]);
                DrawRectangle(slider.x, slider.y + 9, slider.width, 3, muted);
                DrawCircle(static_cast<int>(slider.x + slider.width * values[channel] / 255.0f), slider.y + 10, 5, channels[channel]);
                DrawText(TextFormat("%03d", values[channel]), colorPanel.x + 190, slider.y + 4, 12, gold);
            }
            DrawRectangle(colorPanel.x + 10, colorPanel.y + 125, 225, 8, color);
        }
        if (!editing && showSavedMenu)
        {
            DrawRectangleRec(savedMenu, {24, 27, 28, 255});
            DrawRectangleLinesEx(savedMenu, 1, muted);
            if (!catalogStatus.empty()) DrawText(catalogStatus.c_str(), savedMenu.x + 8, savedMenu.y + 10, 10, gold);
            else if (savedFiles.empty()) DrawText("No hay modelos guardados", savedMenu.x + 8, savedMenu.y + 10, 12, gold);
            else
            {
                for (int i = 0; i < 7 && savedScroll + i < static_cast<int>(savedFiles.size()); ++i)
                {
                    Rectangle item = {savedMenu.x + 4, savedMenu.y + 5 + i * 26.0f, savedMenu.width - 8, 24};
                    if (CheckCollisionPointRec(mouse, item)) DrawRectangleRec(item, {60, 68, 69, 255});
                    std::string label = savedFiles[savedScroll + i].filename().string();
                    if (label.size() > 29) label = label.substr(0, 26) + "...";
                    DrawText(label.c_str(), item.x + 5, item.y + 6, 12, gold);
                }
                if (savedFiles.size() > 7) DrawText("RUEDA: VER MAS", savedMenu.x + 8, savedMenu.y + 186, 10, muted);
            }
        }
        EndDrawing();
        if (maskPixels) UnloadImageColors(maskPixels);
        if (pixels) UnloadImageColors(pixels);
        UnloadImage(image);
    }
    UnloadRenderTexture(scene);
    UnloadRenderTexture(mask);
    UnloadShader(lighting);
    UnloadShader(maskShader);
    UnloadFont(asciiFont);
    CloseWindow();
    return 0;
}
