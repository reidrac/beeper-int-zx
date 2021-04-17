
// sfxed for beeper engine
// Copyright (C) 2021 by Juan J. Martinez <jjm@usebox.net>
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
#include "imgui.h"
#include "imgui_impl_sdl.h"
#include "imgui_impl_opengl3.h"

#include "ImGuiFileDialog.h"

#include <stdio.h>
#include <ctype.h>

#include "SDL.h"

#include <GL/gl3w.h>

extern "C" {
#include "sfx.h"
}

#define APP_NAME "SFX Editor"
#define APP_URL "https://github.com/reidrac/beeper-int-zx"

void show_help(char *argv0) {
    printf("Usage: %s [options] <filename.sfx>\n\n"
           "Available options:\n"
           "  -h, --help      this help text\n"
           "  -V, --version   show version and exit\n\n",
           argv0);
}

#define MAX_ENTRIES 255
BeeperSfx sfx[MAX_ENTRIES];
int entries;

void add_entry(uint8_t index)
{
    if (index != entries)
        for (int i = entries; i > index; i--)
        {
            sfx[i].type = sfx[i - 1].type;
            sfx[i].frames = sfx[i - 1].frames;
            sfx[i].freq = sfx[i - 1].freq;
            sfx[i].slide = sfx[i - 1].slide;
            sfx[i].next = sfx[i - 1].next;
            strcpy(sfx[i].name, sfx[i - 1].name);
            if (sfx[i].next)
                sfx[i].next++;
        }

    sfx[index].type = 1;
    sfx[index].frames = 12;
    sfx[index].freq = 32;
    sfx[index].slide = 0;
    sfx[index].next = 0;
    strcpy(sfx[index].name, "sfx");
    entries++;
}

void remove_entry(uint8_t index)
{
    if (index + 1 == entries)
    {
        entries--;
        return;
    }

    for (int i = index + 1; i < entries; i++)
    {
        sfx[i - 1].type = sfx[i].type;
        sfx[i - 1].frames = sfx[i].frames;
        sfx[i - 1].freq = sfx[i].freq;
        sfx[i - 1].slide = sfx[i].slide;
        sfx[i - 1].next = sfx[i].next;
        strcpy(sfx[i - 1].name, sfx[i].name);
    }

    entries--;
    for (int i = 0; i < entries; i++)
        if (sfx[i].next == index + 1)
            sfx[i].next = 0;
        else if (sfx[i].next > index + 1)
            sfx[i].next--;
}

int main(int argc, char *argv[])
{
    char *filename = NULL;
    char *error = NULL;

    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help"))
        {
            show_help(argv[0]);
            return 0;
        }
        else if (!strcmp(argv[i], "-V") || !strcmp(argv[i], "--version"))
        {
            printf(APP_NAME " " APP_VERSION "\n"
                   APP_URL "\n");
            return 0;
        }
        else if (!filename)
            filename = strdup(argv[i]);
        else
        {
            fprintf(stderr, "ERROR: unsupported option '%s', try -h\n", argv[i]);
            return 1;
        }

    entries = 0;
    if (filename)
    {
        entries = load_sfx(filename, sfx, MAX_ENTRIES);
        if (entries == -1)
            return 1;
    }
    else
        add_entry(0);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_AUDIO) != 0)
    {
        fprintf(stderr, "ERROR: %s\n", SDL_GetError());
        return 1;
    }

    // GL 3.0 + GLSL 130
    const char* glsl_version = "#version 130";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);

    // Create window with graphics context
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Window* window = SDL_CreateWindow(APP_NAME " " APP_VERSION, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 750, 360, window_flags);
    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    SDL_GL_MakeCurrent(window, gl_context);
    SDL_GL_SetSwapInterval(1); // Enable vsync

    // Initialize OpenGL loader
    bool err = gl3wInit() != 0;
    if (err)
    {
        fprintf(stderr, "ERROR: failed to initialize OpenGL loader!\n");
        return 1;
    }

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

    // Setup Platform/Renderer backends
    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init(glsl_version);

    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    const char * types_names[] = { "Silence", "Tone", "Noise" };

    bool done = false;
    while (!done)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT)
                done = true;
            if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE && event.window.windowID == SDL_GetWindowID(window))
                done = true;
        }

        // Start the Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame(window);
        ImGui::NewFrame();

        if (ImGuiFileDialog::Instance()->Display("OpenFileDlgKey"))
        {
            if (ImGuiFileDialog::Instance()->IsOk())
            {
                std::string filePathName = ImGuiFileDialog::Instance()->GetFilePathName();
                if (filename)
                    free(filename);
                filename = strdup(filePathName.c_str());
                entries = load_sfx(filename, sfx, MAX_ENTRIES);
                if (entries == -1)
                {
                    entries = 0;
                    add_entry(0);
                    if (filename)
                        free(filename);
                    filename = NULL;

                    error = strdup("Failed to load the SFX file!\n\nIt is possible that the file is corrupt or is not a sfx file.");
                }
            }
            ImGuiFileDialog::Instance()->Close();
        }

        if (ImGuiFileDialog::Instance()->Display("SaveAsFileDlgKey"))
        {
            if (ImGuiFileDialog::Instance()->IsOk())
            {
                std::string filePathName = ImGuiFileDialog::Instance()->GetFilePathName();
                if (filename)
                    free(filename);
                filename = strdup(filePathName.c_str());
                if (save_sfx(filename, sfx, entries) == -1)
                    error = strdup("Failed to save the SFX file!\n\nPlease double check that you have permissions to save on that location.");
            }
            ImGuiFileDialog::Instance()->Close();
        }

        if (ImGuiFileDialog::Instance()->Display("ExportFileDlgKey"))
        {
            if (ImGuiFileDialog::Instance()->IsOk())
            {
                std::string filePathName = ImGuiFileDialog::Instance()->GetFilePathName();
                if (export_sfx((char *)filePathName.c_str(), sfx, entries) == -1)
                    error = strdup("Failed to export!\n\nPlease double check that you have permissions to save on that location.");
            }
            ImGuiFileDialog::Instance()->Close();
        }

        ImGui::Begin(filename ? filename : "<not saved>", NULL, 0);

        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("New"))
                {
                    if (filename)
                        free(filename);
                    filename = NULL;
                    entries = 0;
                    add_entry(0);
                }
                if (ImGui::MenuItem("Open", ""))
                    ImGuiFileDialog::Instance()->OpenDialog("OpenFileDlgKey",
                                                            "Open File", ".sfx", ".");
                if (ImGui::MenuItem("Save", "", false, filename != NULL))
                {
                    if (save_sfx(filename, sfx, entries) == -1)
                        error = strdup("Failed to save the SFX file!\n\nPlease double check that you have permissions to save on that location.");
                }
                if (ImGui::MenuItem("Save As"))
                    ImGuiFileDialog::Instance()->OpenDialog("SaveAsFileDlgKey",
                                                            "Save As", ".sfx", ".");

                ImGui::Separator();
                if (ImGui::MenuItem("Quit", "Alt+F4"))
                    done = true;
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Export")) {
                if (ImGui::MenuItem("Binary"))
                    ImGuiFileDialog::Instance()->OpenDialog("ExportFileDlgKey",
                                                            "Export As", ".bin", ".");
                if (ImGui::MenuItem("C include"))
                    ImGuiFileDialog::Instance()->OpenDialog("ExportFileDlgKey",
                                                            "Export As", ".h", ".");
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        if (ImGui::BeginTable("effects table", 8, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg))
        {
            ImGui::TableSetupColumn("No.");
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 100.0f);
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableSetupColumn("Frames", ImGuiTableColumnFlags_WidthFixed, 60.0f);
            ImGui::TableSetupColumn("Freq", ImGuiTableColumnFlags_WidthFixed, 60.0f);
            ImGui::TableSetupColumn("Slide", ImGuiTableColumnFlags_WidthFixed, 60.0f);
            ImGui::TableSetupColumn("Next", ImGuiTableColumnFlags_WidthFixed, 60.0f);
            ImGui::TableHeadersRow();

            for (int i = 0; i < entries; i++)
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("%02d", i + 1);

                ImGui::PushID(i);

                ImGui::TableNextColumn();
                ImGui::PushID("name");
                ImGui::InputText("", sfx[i].name, 9, ImGuiInputTextFlags_CharsNoBlank);
                ImGui::PopID();

                ImGui::TableNextColumn();
                ImGui::PushID("type");
                ImGui::Combo("", &sfx[i].type, types_names, IM_ARRAYSIZE(types_names), IM_ARRAYSIZE(types_names));
                ImGui::PopID();

                ImGui::TableNextColumn();
                ImGui::PushID("fames");
                ImGui::InputScalar("", ImGuiDataType_U8, &sfx[i].frames);
                ImGui::PopID();
                ImGui::TableNextColumn();
                ImGui::PushID("freq");
                ImGui::InputScalar("", ImGuiDataType_U8, &sfx[i].freq);
                ImGui::PopID();
                ImGui::TableNextColumn();
                ImGui::PushID("slide");
                ImGui::InputScalar("", ImGuiDataType_S8, &sfx[i].slide);
                ImGui::PopID();
                ImGui::TableNextColumn();
                ImGui::PushID("next");
                ImGui::InputScalar("", ImGuiDataType_U8, &sfx[i].next);
                ImGui::PopID();
                ImGui::TableNextColumn();

                if (sfx[i].frames == 0)
                    sfx[i].frames = 1;
                if (sfx[i].freq == 0)
                    sfx[i].freq = 1;

                if (sfx[i].next == i + 1 || sfx[i].next > entries)
                {
                    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(255, 0, 0, 100));
                    ImGui::Button("Play");
                    ImGui::PopStyleColor();
                }
                else if (ImGui::Button("Play"))
                    play_sfx(i + 1, sfx, entries);
                ImGui::SameLine();
                if (entries < MAX_ENTRIES)
                {
                    if (ImGui::Button("+"))
                        add_entry(i + 1);
                }
                if (entries > 1)
                {
                    ImGui::SameLine();
                    if (ImGui::Button("-"))
                        remove_entry(i);
                }

                ImGui::PopID();
            }
            ImGui::EndTable();
        }

        if (error && !ImGui::IsPopupOpen("Error"))
            ImGui::OpenPopup("Error");
        if (ImGui::BeginPopupModal("Error", NULL, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text(error);
            ImGui::Separator();
            if (ImGui::Button("OK", ImVec2(120, 0)))
            {
                if (error)
                    free(error);
                error = NULL;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SetItemDefaultFocus();
            ImGui::EndPopup();
        }

        ImGui::End();

        // Rendering
        ImGui::Render();
        glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
        glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
