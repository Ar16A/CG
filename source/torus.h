

typedef struct
{
    float x;
    float y;
    float z;

    float nx;
    float ny;
    float nz;
} TorusVertex;

typedef struct
{
    TorusVertex* vertices;
    unsigned int* indices;

    unsigned int vertex_count;
    unsigned int index_count;
} TorusMesh;

TorusMesh torus_create(
    float major_radius,
    float minor_radius,
    unsigned int segments,
    unsigned int rings
);

void torus_destroy(TorusMesh* mesh);