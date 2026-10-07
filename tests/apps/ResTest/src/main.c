// ResTest: a picture with slopes in it (CC0).
//
// Through GXM (vita2d), the same every frame: a dark background, lines from
// the middle of the screen at eight angles none of which is a row or a
// column, a disc, and a tilted triangle. Nothing is antialiased, so the
// picture holds four colours and no others.
//
// What it is for: a slope drawn on a grid is a staircase, and the size of
// its steps is the grid's. Drawn at twice the Vita's resolution the steps
// are half the size - which a 960x544 picture stretched to 1920x1088 cannot
// show, whichever way it is stretched: doubled pixels keep the big steps,
// and a smoothing filter makes colours the picture never had.
#include <psp2/kernel/processmgr.h>
#include <vita2d.h>

int main(int argc, char *argv[]) {
    static const float ends[8][2] = {
        { 930.0f, 61.0f }, { 870.0f, 500.0f }, { 600.0f, 530.0f }, { 310.0f, 520.0f },
        { 40.0f, 430.0f }, { 25.0f, 120.0f }, { 350.0f, 15.0f }, { 640.0f, 22.0f },
    };
    vita2d_init();
    vita2d_set_clear_color(RGBA8(16, 24, 48, 255));
    for (;;) {
        vita2d_start_drawing();
        vita2d_clear_screen();
        for (int i = 0; i < 8; i++)
            vita2d_draw_line(480.0f, 272.0f, ends[i][0], ends[i][1], RGBA8(255, 255, 255, 255));
        vita2d_draw_fill_circle(200.0f, 150.0f, 71.0f, RGBA8(255, 160, 0, 255));
        // a triangle, as a fan of lines would not be: three vertices, one colour
        {
            vita2d_color_vertex *v = (vita2d_color_vertex *)vita2d_pool_memalign(3 * sizeof(vita2d_color_vertex), sizeof(vita2d_color_vertex));
            const float xy[3][2] = { { 700.0f, 300.0f }, { 905.0f, 395.0f }, { 733.0f, 493.0f } };
            for (int i = 0; i < 3; i++) {
                v[i].x = xy[i][0];
                v[i].y = xy[i][1];
                v[i].z = 0.5f;
                v[i].color = RGBA8(0, 200, 120, 255);
            }
            vita2d_draw_array(SCE_GXM_PRIMITIVE_TRIANGLES, v, 3);
        }
        vita2d_end_drawing();
        vita2d_swap_buffers();
    }
    return 0;
}
