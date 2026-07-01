#include "imgui.h"
#include "imgui_impl_raylib.h"
#include "raylib.h"
#include "rlgl.h"

static bool s_initialized = false;
static Texture2D s_fontTex = { 0 };

static void ImGui_ImplRaylib_SetupBackend(unsigned char* pixels, int w, int h) {
    Image img = { 0 };
    img.data = pixels;
    img.width = w;
    img.height = h;
    img.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
    img.mipmaps = 1;
    s_fontTex = LoadTextureFromImage(img);
    ImGui::GetIO().Fonts->TexID = (ImTextureID)(intptr_t)s_fontTex.id;
}

bool ImGui_ImplRaylib_Init(int width, int height) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)width, (float)height);
    io.DeltaTime = 1.0f / 60.0f;
    io.IniFilename = NULL;

    ImGui::StyleColorsDark();

    unsigned char* pixels = NULL;
    int w = 0, h = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
    ImGui_ImplRaylib_SetupBackend(pixels, w, h);

    s_initialized = true;
    return true;
}

void ImGui_ImplRaylib_Shutdown(void) {
    if (s_initialized) {
        UnloadTexture(s_fontTex);
        ImGui::DestroyContext();
        s_initialized = false;
    }
}

void ImGui_ImplRaylib_NewFrame(void) {
    ImGuiIO& io = ImGui::GetIO();
    io.DeltaTime = GetFrameTime();

    Vector2 mouse = GetMousePosition();
    io.MousePos = ImVec2(mouse.x, mouse.y);
    io.MouseDown[0] = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    io.MouseDown[1] = IsMouseButtonDown(MOUSE_BUTTON_RIGHT);
    io.MouseDown[2] = IsMouseButtonDown(MOUSE_BUTTON_MIDDLE);
    io.MouseWheel = GetMouseWheelMove();

    io.KeyCtrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    io.KeyShift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    io.KeyAlt = IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT);

    int key = GetCharPressed();
    while (key > 0) {
        io.AddInputCharacter((unsigned int)key);
        key = GetCharPressed();
    }

    ImGui::NewFrame();
}

void ImGui_ImplRaylib_RenderDrawData(void) {
    ImGui::Render();
    ImDrawData* drawData = ImGui::GetDrawData();
    if (!drawData || drawData->CmdListsCount == 0) return;

    rlDisableBackfaceCulling();
    rlDisableDepthMask();
    rlSetBlendMode(BLEND_ALPHA);

    for (int l = 0; l < drawData->CmdListsCount; l++) {
        const ImDrawList* cmdList = drawData->CmdLists[l];
        for (int c = 0; c < cmdList->CmdBuffer.Size; c++) {
            const ImDrawCmd* pcmd = &cmdList->CmdBuffer[c];
            if (pcmd->UserCallback) {
                pcmd->UserCallback(cmdList, pcmd);
                continue;
            }
            rlSetTexture((unsigned int)(intptr_t)pcmd->TextureId);
            rlScissor(
                (int)pcmd->ClipRect.x,
                GetScreenHeight() - (int)(pcmd->ClipRect.y + pcmd->ClipRect.w),
                (int)(pcmd->ClipRect.z - pcmd->ClipRect.x),
                (int)(pcmd->ClipRect.w - pcmd->ClipRect.y)
            );
            rlEnableScissorTest();
            rlBegin(RL_TRIANGLES);
            for (unsigned int i = 0; i < pcmd->ElemCount; i++) {
                const ImDrawVert* v = &cmdList->VtxBuffer[cmdList->IdxBuffer[i]];
                rlColor4ub(
                    (v->col >> IM_COL32_R_SHIFT) & 0xFF,
                    (v->col >> IM_COL32_G_SHIFT) & 0xFF,
                    (v->col >> IM_COL32_B_SHIFT) & 0xFF,
                    (v->col >> IM_COL32_A_SHIFT) & 0xFF
                );
                rlTexCoord2f(v->uv.x, v->uv.y);
                rlVertex2f(v->pos.x, v->pos.y);
            }
            rlEnd();
        }
    }
    rlDisableScissorTest();
    rlSetTexture(0);
}
