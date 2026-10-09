#include "torus.h"

#include <math.h>
#include <stdlib.h>

#define TORUS_PI 3.14159265358979323846f

TorusMesh torus_create(
    float major_radius,
    float minor_radius,
    unsigned int segments,
    unsigned int rings
)
{
    TorusMesh mesh = { 0 };

    if (segments < 3 || rings < 3)
    {
        return mesh;
    }

    mesh.vertex_count = segments * rings;
    mesh.index_count = segments * rings * 6;

    mesh.vertices = malloc(
        mesh.vertex_count * sizeof(TorusVertex)
    );

    mesh.indices = malloc(
        mesh.index_count * sizeof(unsigned int)
    );

    if (mesh.vertices == NULL || mesh.indices == NULL)
    {
        free(mesh.vertices);
        free(mesh.indices);

        mesh.vertices = NULL;
        mesh.indices = NULL;

        mesh.vertex_count = 0;
        mesh.index_count = 0;

        return mesh;
    }

    /*
     * Параметрическое уравнение тора:
     *
     * x = (R + r*cos(theta))*cos(phi)
     * y = (R + r*cos(theta))*sin(phi)
     * z = r*sin(theta)
     */

    for (unsigned int i = 0; i < segments; ++i)
    {
        float phi =
            2.0f * TORUS_PI *
            (float)i /
            (float)segments;

        float cos_phi = cosf(phi);
        float sin_phi = sinf(phi);

        for (unsigned int j = 0; j < rings; ++j)
        {
            float theta =
                2.0f * TORUS_PI *
                (float)j /
                (float)rings;

            float cos_theta = cosf(theta);
            float sin_theta = sinf(theta);

            unsigned int index =
                i * rings + j;

            mesh.vertices[index].x =
                (major_radius +
                    minor_radius * cos_theta) *
                cos_phi;

            mesh.vertices[index].y =
                (major_radius +
                    minor_radius * cos_theta) *
                sin_phi;

            mesh.vertices[index].z =
                minor_radius * sin_theta;

            /*
             * Нормаль.
             */

            mesh.vertices[index].nx =
                cos_theta * cos_phi;

            mesh.vertices[index].ny =
                cos_theta * sin_phi;

            mesh.vertices[index].nz =
                sin_theta;
        }
    }

    /*
     * Соединяем вершины треугольниками.
     */

    unsigned int index = 0;

    for (unsigned int i = 0; i < segments; ++i)
    {
        unsigned int next_i =
            (i + 1) % segments;

        for (unsigned int j = 0; j < rings; ++j)
        {
            unsigned int next_j =
                (j + 1) % rings;

            unsigned int A =
                i * rings + j;

            unsigned int B =
                next_i * rings + j;

            unsigned int C =
                next_i * rings + next_j;

            unsigned int D =
                i * rings + next_j;

            mesh.indices[index++] = A;
            mesh.indices[index++] = B;
            mesh.indices[index++] = C;

            mesh.indices[index++] = A;
            mesh.indices[index++] = C;
            mesh.indices[index++] = D;
        }
    }

    return mesh;
}

void torus_destroy(TorusMesh* mesh)
{
    if (mesh == NULL)
    {
        return;
    }

    free(mesh->vertices);
    free(mesh->indices);

    mesh->vertices = NULL;
    mesh->indices = NULL;

    mesh->vertex_count = 0;
    mesh->index_count = 0;
}