
// GhostEngineDataCore.h

void GhostEngine_Tick()
{
    if (!g_rhi || !g_pipeline) return;

    // ---- Delta time ----
    LARGE_INTEGER curr;
    QueryPerformanceCounter(&curr);
    float dt = (float)(curr.QuadPart - g_prev.QuadPart) / (float)g_freq.QuadPart;
    g_prev = curr;
    g_timeSec = fmodf(g_timeSec + (dt > 0.25f ? 0.25f : dt), 3600.0f);

    if (dt > 1e-5f)
    {
        const float inst = 1.0f / dt;
        g_fps = (g_fps <= 0.0f) ? inst : g_fps + (inst - g_fps) * 0.05f;
    }
    // a drag never outlives the mouse button (lost focus, Play pressed mid-drag)
    if (g_gizmo.active && (g_playState != GhostPlayState::Editing ||
        !((GetAsyncKeyState(VK_LBUTTON) | GetAsyncKeyState(VK_RBUTTON)) & 0x8000)))
        g_gizmo.active = false;

    // ---- Play mode: sample input, then run the fixed-timestep simulation ----
    g_input.Sample(g_inputEnabled && g_playState == GhostPlayState::Playing);
    if (g_playState == GhostPlayState::Playing)
    {
        const int steps = g_stepper.Advance(dt);
        if (steps > 0)                               // hand the mouse movement collected since the last step to the game
        {
            g_apiMouseDX = g_mouseAccumX; g_apiMouseDY = g_mouseAccumY; g_apiWheel = g_wheelAccum;
            g_mouseAccumX = g_mouseAccumY = 0.0f; g_wheelAccum = 0.0f;
        }
        bool faulted = false;
        for (int i = 0; i < steps; i++)
        {
            g_uiQuads.clear();                       // each step redraws the whole HUD; a frame with no step keeps the last one
            if (!GhostGameModule_Update(GHOST_FIXED_DT))
            {
                g_playFault = "The game module crashed inside GhostGame_Update, so Play was stopped.\n"
                    "Fix the code, rebuild GhostGame and press Play again.";
                faulted = true;
                break;
            }
            g_playTime += GHOST_FIXED_DT;
            g_input.EndStep();
            g_apiMouseDX = g_apiMouseDY = 0.0f; g_apiWheel = 0.0f;    // only the first step of a frame sees the movement
            if (!g_pendingScene.empty()) break;              // switch scenes before running more steps
        }
        if (faulted) GhostEngine_Stop();
        else if (!g_pendingScene.empty()) PerformSceneSwitch();
    }

    if (g_playState == GhostPlayState::Playing)       // time of day, layer drift, cloud drift and light spin animate only while playing
    {
        for (GhostModEntity& m : g_scene.mods)
            if (m.typeId == GHOST_MOD_SKY)
            {
                float ds, ns, cu, cv;
                GhostSky_Speeds(m, ds, ns);
                GhostSky_CloudSpeeds(m, cu, cv);
                GhostSky_AdvanceTime(m, dt);
                g_skyScroll[0] = fmodf(g_skyScroll[0] + ds * dt, 360.0f);
                g_skyScroll[1] = fmodf(g_skyScroll[1] + ns * dt, 360.0f);
                g_cloudScroll[0] = fmodf(g_cloudScroll[0] + cu * dt, 64.0f);   // 64 keeps the wave phase seamless too
                g_cloudScroll[1] = fmodf(g_cloudScroll[1] + cv * dt, 64.0f);
            }
        for (GhostLight& l : g_scene.lights)          // Stop restores the backup, so the spin never touches your saved angles
        {
            if (l.spinYaw != 0.0f)   l.yawDeg = fmodf(l.yawDeg + l.spinYaw * dt, 360.0f);
            if (l.spinPitch != 0.0f) l.pitchDeg = fmodf(l.pitchDeg + l.spinPitch * dt, 360.0f);
        }
    }

    // ---- Active camera -> working state (the fly code below keeps using g_eye / g_yaw / g_pitch) ----
    GhostCamera* cam = InputCamera();
    if (cam)
    {
        g_eye = cam->position;
        g_yaw = cam->yawDeg * (GHOST_PI / 180.0f);
        g_pitch = cam->pitchDeg * (GHOST_PI / 180.0f);
    }
    const bool flyEnabled = g_inputEnabled && g_playState == GhostPlayState::Editing;   // the editor fly cam is off while playing
    // ---- Mouse look ----
    const float lookSens = 0.0025f;
    const float moveSpeed = cam ? cam->moveSpeed : 3.5f;

    const bool orbitCam = cam && cam->mode == 1;
    const bool editing = g_playState == GhostPlayState::Editing;
    const bool playing = g_playState == GhostPlayState::Playing;
    const bool lookEnabled = g_inputEnabled && (editing || playing);
    const bool applyLook = editing || orbitCam;      // while playing, only an orbit camera is turned by the engine
    const float toRad = GHOST_PI / 180.0f;

    if (lookEnabled && (GetAsyncKeyState(VK_RBUTTON) & 0x8000))
    {
        if (!g_looking)
        {
            g_looking = true;
            GetCursorPos(&g_lastMouse);
            ShowCursor(FALSE);
        }
        else
        {
            POINT cur;
            GetCursorPos(&cur);
            int dx = cur.x - g_lastMouse.x;
            int dy = cur.y - g_lastMouse.y;
            if (playing) { g_mouseAccumX += (float)dx; g_mouseAccumY += (float)dy; }     // the game can read it too

            if (applyLook)
            {
                g_yaw += dx * lookSens;
                g_pitch -= dy * lookSens;
                float hi = orbitCam ? cam->maxPitchDeg * toRad : GHOST_PI * 0.49f;
                float lo = orbitCam ? cam->minPitchDeg * toRad : -GHOST_PI * 0.49f;
                if (g_pitch > hi) g_pitch = hi;
                if (g_pitch < lo) g_pitch = lo;
            }
            SetCursorPos(g_lastMouse.x, g_lastMouse.y);
        }
    }
    else if (g_looking)
    {
        g_looking = false;
        ShowCursor(TRUE);
    }
    if (playing && orbitCam && g_inputEnabled && GhostPad_Connected())      // right stick orbits, triggers zoom
    {
        const float rx = GhostPad_Axis(GHOST_PAD_AXIS_RX), ry = GhostPad_Axis(GHOST_PAD_AXIS_RY);
        if (rx != 0.0f || ry != 0.0f)
        {
            g_yaw += rx * 2.6f * dt;
            g_pitch += ry * 1.6f * dt;
            const float hi = cam->maxPitchDeg * toRad, lo = cam->minPitchDeg * toRad;
            if (g_pitch > hi) g_pitch = hi;
            if (g_pitch < lo) g_pitch = lo;
        }
        const float zin = GhostPad_Axis(GHOST_PAD_AXIS_RT), zout = GhostPad_Axis(GHOST_PAD_AXIS_LT);
        if (zin != zout)                                                    // R2 in, L2 out (same direction as the wheel)
        {
            float d = cam->distance - (zin - zout) * 8.0f * dt;
            d = d < cam->minDist ? cam->minDist : (d > cam->maxDist ? cam->maxDist : d);
            cam->distance = d;
        }
    }

    const bool modHeld = ((GetAsyncKeyState(VK_CONTROL) | GetAsyncKeyState(VK_MENU)) & 0x8000) != 0;
    auto down = [&](int vk) { return flyEnabled && !modHeld && (GetAsyncKeyState(vk) & 0x8000) != 0; };
    // ---- Movement ----
    GhostVec3 forward = {
        cosf(g_pitch) * sinf(g_yaw),
        sinf(g_pitch),
        cosf(g_pitch) * cosf(g_yaw)
    };
    forward = forward.Normalize();
    GhostVec3 worldUp = { 0.0f, 1.0f, 0.0f };
    GhostVec3 right = worldUp.Cross(forward).Normalize();   // was forward.Cross(worldUp)
    float speed = moveSpeed * dt;

    if (down('W'))        g_eye = g_eye + forward * speed;
    if (down('S'))        g_eye = g_eye - forward * speed;
    if (down('A'))        g_eye = g_eye - right * speed;
    if (down('D'))        g_eye = g_eye + right * speed;
    if (down(VK_SPACE) || down('E')) g_eye.y += speed;
    if (down('Q'))                   g_eye.y -= speed;
    // ---- Working state -> active camera ----
    if (cam)
    {
        cam->position = g_eye;
        cam->yawDeg = fmodf(g_yaw * (180.0f / GHOST_PI), 360.0f);
        cam->pitchDeg = g_pitch * (180.0f / GHOST_PI);
    }
    UpdateOrbitCameras(dt, g_playState != GhostPlayState::Editing);
    UpdateSprites(dt); // animation clips run in the editor and while playing
    UpdateAnimators(dt);
    PaintTick(dt);
    UpdateTileMaps();
    GhostAudio_Update(dt);

    if (g_playState == GhostPlayState::Editing)
    {
        UndoTick(dt);
        PruneMulti();
        if (g_marquee && !(GetAsyncKeyState(VK_LBUTTON) & 0x8000)) g_marquee = false;
    }
    if (g_pipeline->shadowSize != (unsigned int)g_scene.shadowRes)        // shadow resolution changed
        if (GhostSetShadowSize(g_rhi, g_pipeline, (unsigned int)g_scene.shadowRes) != GHOST_OK)
            g_scene.shadowRes = (int)g_pipeline->shadowSize;              // could not allocate: keep what works
    GhostMSAA_Apply(g_rhi, g_pipeline, g_msaa, g_taaOn ? 1 : g_msaaWanted);   // TAA and MSAA are exclusive
    // ---- Render: camera windows first, the main view last (it carries the vsync) ----
    for (int vp = 1; vp < GHOST_MAX_SURFACES; vp++)
        if (g_vp[vp].alive) RenderViewport(vp);
    RenderViewport(0);
    g_rhi->SetCurrentSurface(0);
}

void GhostEngine_Resize(unsigned int width, unsigned int height)
{
    if (!g_rhi || !g_pipeline) return;
    if (width == 0 || height == 0) return;
    g_width = width;
    g_height = height;
    g_rhi->SetCurrentSurface(0);
    g_rhi->Resize(width, height);
    // GhostEnsureDepth creates depth buffer
  //  GhostResizeDepthBuffer(g_rhi, g_pipeline, 0, width, height);
}

void GhostEngine_Shutdown()
{
    GhostPad_Shutdown();
    GhostGameModule_Unload();
    GhostAudio_Shutdown();
    GhostMSAA_Shutdown(g_msaa);
    GhostTAA_Shutdown(g_taa);

    GhostPipelineDestroy(g_pipeline);
    g_pipeline = nullptr;

    if (g_rhi)
    {
        g_rhi->Shutdown();
        delete g_rhi;
        g_rhi = nullptr;
    }
}

// ============================================================
//  Exported: faces / materials
// ============================================================
int GhostEngine_GetFaceCount(int objectId)
{
    GhostObject* o = FindObject(objectId);
    return o ? GhostObject_FaceCount(*o) : 0;
}

int GhostEngine_GetFaceName(int objectId, int face, char* buf, int bufSize)
{
    GhostObject* o = FindObject(objectId);
    if (!o || !buf || bufSize <= 0 || face < 0 || face >= GhostObject_FaceCount(*o)) return 0;
    if (o->tileMap.enabled) { snprintf(buf, (size_t)bufSize, "Layer %d", face); return 1; }
    strncpy_s(buf, bufSize, GhostMesh_Face(o->meshId, face).name.c_str(), _TRUNCATE);
    return 1;
}

int GhostEngine_GetMaterial(int objectId, int face, GhostMaterialInfo* out)
{
    GhostObject* o = FindObject(objectId);
    if (!o || !out) return 0;
    if (face < 0) face = 0;
    if (face >= GhostObject_FaceCount(*o)) return 0;
    const GhostMaterial& m = o->faces[face];
    *out = { m.textureId, m.mapMode, m.wrap, m.filter,
             m.tiling[0], m.tiling[1], m.offset[0], m.offset[1], m.rotationDeg,
             m.tint[0], m.tint[1], m.tint[2],
             m.metallic, m.roughness, m.specular,
             m.emissive[0], m.emissive[1], m.emissive[2], m.emissiveStrength,
             m.alphaMode, m.alphaSource, m.alphaCutoff };
    return 1;
}

void GhostEngine_SetMaterial(int objectId, int face, const GhostMaterialInfo* in)
{
    GhostObject* o = FindObject(objectId);
    if (!o || !in) return;
    const int count = GhostObject_FaceCount(*o);
    if (face >= count) return;

    auto nz = [](float v) { return fabsf(v) < 0.001f ? (v < 0.0f ? -0.001f : 0.001f) : v; };
    auto c01 = [](float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); };
    const int tex = (in->textureId >= 0 && in->textureId < (int)g_scene.textures.size()) ? in->textureId : 0;

    const int from = face < 0 ? 0 : face;
    const int to = face < 0 ? count - 1 : face;
    for (int f = from; f <= to; f++)
    {
        GhostMaterial m = o->faces[f];                  // keep the maps and advanced surface values
        m.textureId = tex;
        m.mapMode = in->mapMode < 0 ? 0 : (in->mapMode > 7 ? 7 : in->mapMode);
        m.wrap = in->wrap ? 1 : 0;
        m.filter = in->filter ? 1 : 0;
        m.tiling[0] = nz(in->tilingU); m.tiling[1] = nz(in->tilingV);
        m.offset[0] = in->offsetU;     m.offset[1] = in->offsetV;
        m.rotationDeg = in->rotationDeg;
        m.tint[0] = c01(in->r); m.tint[1] = c01(in->g); m.tint[2] = c01(in->b);
        m.metallic = c01(in->metallic);
        m.roughness = in->roughness < 0.02f ? 0.02f : (in->roughness > 1.0f ? 1.0f : in->roughness);
        m.specular = c01(in->specular);
        m.emissive[0] = c01(in->emR); m.emissive[1] = c01(in->emG); m.emissive[2] = c01(in->emB);
        m.emissiveStrength = in->emissiveStrength < 0.0f ? 0.0f : (in->emissiveStrength > 20.0f ? 20.0f : in->emissiveStrength);
        m.alphaMode = in->alphaMode < 0 ? 0 : (in->alphaMode > 2 ? 2 : in->alphaMode);
        m.alphaSource = in->alphaSource < 0 ? 0 : (in->alphaSource > 2 ? 2 : in->alphaSource);
        m.alphaCutoff = c01(in->alphaCutoff);
        o->faces[f] = m;
    }
    if (o->meshId == GHOST_MESH_QUAD) o->sprite.textureId = tex;
}

static const float kMapMax[GHOST_MAP_COUNT] = { 4, 1, 1, 1, 1, 20, 1, 1, 1, 5, 1, 10, 1 };

int GhostEngine_GetMap(int objectId, int face, int slot, GhostMapInfo* out)
{
    GhostObject* o = FindObject(objectId);
    if (!o || !out || slot < 0 || slot >= GHOST_MAP_COUNT) return 0;
    if (face < 0) face = 0;
    if (face >= GhostObject_FaceCount(*o)) return 0;
    const GhostMapParams& p = o->faces[face].maps[slot];
    *out = { p.textureId, p.amount, p.channel, p.invert };
    return 1;
}

void GhostEngine_SetMap(int objectId, int face, int slot, const GhostMapInfo* in)
{
    GhostObject* o = FindObject(objectId);
    if (!o || !in || slot < 0 || slot >= GHOST_MAP_COUNT) return;
    const int count = GhostObject_FaceCount(*o);
    if (face >= count) return;

    const float mx = kMapMax[slot], mn = (slot == GHOST_MAP_WPO) ? -mx : 0.0f;
    GhostMapParams p;
    p.textureId = (in->textureId >= 0 && in->textureId < (int)g_scene.textures.size()) ? in->textureId : 0;
    p.amount = in->amount != in->amount ? 0.0f : (in->amount < mn ? mn : (in->amount > mx ? mx : in->amount));
    p.channel = in->channel < 0 ? 0 : (in->channel > 4 ? 4 : in->channel);
    p.invert = in->invert ? 1 : 0;
    const int from = face < 0 ? 0 : face, to = face < 0 ? count - 1 : face;
    for (int f = from; f <= to; f++) o->faces[f].maps[slot] = p;
}

int GhostEngine_GetSurface(int objectId, int face, GhostSurfaceInfo* out)
{
    GhostObject* o = FindObject(objectId);
    if (!o || !out) return 0;
    if (face < 0) face = 0;
    if (face >= GhostObject_FaceCount(*o)) return 0;
    const GhostMaterial& m = o->faces[face];
    *out = { m.twoSided, m.opacity, m.refractionIOR, m.reflectionRotDeg, m.anisotropy,
             m.sssColor[0], m.sssColor[1], m.sssColor[2], m.sssAmount,
             m.mapTiling[0], m.mapTiling[1], m.mapOffset[0], m.mapOffset[1],
             m.wpoSway, m.wpoSpeed, m.wpoMid };
    return 1;
}

void GhostEngine_SetSurface(int objectId, int face, const GhostSurfaceInfo* in)
{
    GhostObject* o = FindObject(objectId);
    if (!o || !in) return;
    const int count = GhostObject_FaceCount(*o);
    if (face >= count) return;
    auto cf = [](float v, float lo, float hi) { return v != v ? lo : (v < lo ? lo : (v > hi ? hi : v)); };
    auto nz = [](float v) { return fabsf(v) < 0.001f ? (v < 0.0f ? -0.001f : 0.001f) : v; };

    const int from = face < 0 ? 0 : face, to = face < 0 ? count - 1 : face;
    for (int f = from; f <= to; f++)
    {
        GhostMaterial& m = o->faces[f];
        m.twoSided = in->twoSided ? 1 : 0;
        m.opacity = cf(in->opacity, 0.0f, 1.0f);
        m.refractionIOR = cf(in->refractionIOR, 1.0f, 3.0f);
        m.reflectionRotDeg = cf(in->reflectionRotDeg, -720.0f, 720.0f);
        m.anisotropy = cf(in->anisotropy, -0.95f, 0.95f);
        m.sssColor[0] = cf(in->sssR, 0.0f, 1.0f); m.sssColor[1] = cf(in->sssG, 0.0f, 1.0f); m.sssColor[2] = cf(in->sssB, 0.0f, 1.0f);
        m.sssAmount = cf(in->sssAmount, 0.0f, 1.0f);
        m.mapTiling[0] = nz(cf(in->mapTilingU, -1000.0f, 1000.0f)); m.mapTiling[1] = nz(cf(in->mapTilingV, -1000.0f, 1000.0f));
        m.mapOffset[0] = cf(in->mapOffsetU, -1000.0f, 1000.0f);     m.mapOffset[1] = cf(in->mapOffsetV, -1000.0f, 1000.0f);
        m.wpoSway = cf(in->wpoSway, -5.0f, 5.0f);
        m.wpoSpeed = cf(in->wpoSpeed, 0.0f, 20.0f);
        m.wpoMid = cf(in->wpoMid, 0.0f, 1.0f);
    }
}


int GhostEngine_GetTargetFace() { return CurrentFace(); }

void GhostEngine_SetTargetFace(int face)
{
    CurrentFace();
    GhostObject* o = FindObject(g_scene.selectedId);
    g_scene.selectedFace = (o && face >= 0 && face < GhostObject_FaceCount(*o)) ? face : -1;
}


// ============================================================
//  Exported: texture replace + drop picking
// ============================================================
int GhostEngine_ReplaceTexture(int id, const char* name, const unsigned char* rgba,
    unsigned int width, unsigned int height)
{
    if (!g_rhi || !g_pipeline) return 0;
    if (id < 3 || id >= (int)g_scene.textures.size()) return 0;          // built-ins are fixed
    if (!rgba || width == 0 || height == 0 || width > 4096 || height > 4096) return 0;

    if (GhostCreateTexture(g_rhi, g_pipeline, rgba, width, height, id) != id) return 0;

    GhostTextureInfo& t = g_scene.textures[id];
    t.width = width;
    t.height = height;
    if (name) t.name = name;
    return 1;
}

static int PickObjectAt(int sx, int sy)
{
    GhostVec3 o, d;
    if (!ScreenRay(sx, sy, o, d)) return 0;

    int   bestId = 0;
    float bestT = 1e30f;
    for (const GhostObject& ob : g_scene.objects)
    {
        float byw; GhostMat4 inv = ObjectInverse(ob, BillYaw(ob, g_mainEye, byw));
        float t;
        if (RayAABB(XformPoint(inv, o, 1.0f), XformPoint(inv, d, 0.0f),
            GhostObject_Min(ob), GhostObject_Max(ob), t) && t < bestT)
        {
            bestT = t; bestId = ob.id;
        }
    }
    return bestId;
}

int GhostEngine_PickObjectAt(int px, int py, int* outFace)
{
    if (outFace) *outFace = -1;
    int x, y;
    if (!PanelToRender(px, py, x, y, false)) return 0;
    int id = PickObjectAt(x, y);
    if (id == 0) return 0;
    if (outFace)
        if (GhostObject* ob = FindObject(id)) *outFace = PickFace(*ob, x, y);
    return id;
}

// ============================================================
//  Exported: cameras
// ============================================================
int GhostEngine_AddCamera(int mode, int sx, int sy)
{
    if (g_playState != GhostPlayState::Editing) return 0;
    GhostCamera* c = GhostScene_AddCamera(g_scene);
    if (!c) return 0;

    SpawnCoords(sx, sy);
    GhostVec3 p = SpawnPoint(sx, sy);
    p.y += 1.5f;
    c->position = p;
    if (const GhostCamera* a = ActiveCamera())          // face the same way as the main view
    {
        c->yawDeg = a->yawDeg; c->pitchDeg = a->pitchDeg; c->fovDeg = a->fovDeg;
    }
    if (mode == 1)
    {
        c->mode = 1;
        c->name = "Orbit Camera " + std::to_string(c->id);
        if (sx >= 0)                                    // dropped onto an object: lock to it
        {
            int hit = PickObjectAt(sx, sy);
            if (hit) c->targetId = hit;
        }
    }
    g_scene.selectedId = c->id;
    return c->id;
}

int GhostEngine_GetCamera(int id, GhostCameraInfo* out)
{
    if (!out) return 0;
    for (const GhostCamera& c : g_scene.cameras)
        if (c.id == id)
        {
            *out = { c.id, c.position.x, c.position.y, c.position.z,
                     c.yawDeg, c.pitchDeg, c.fovDeg, c.id == g_scene.activeCameraId ? 1 : 0,
                     c.resW, c.resH,
                     c.mode, c.targetId,
                     c.distance, c.heightOffset, c.minDist, c.maxDist,
                     c.minPitchDeg, c.maxPitchDeg, c.smoothing, c.moveSpeed };
            return 1;
        }
    return 0;
}

void GhostEngine_SetCamera(const GhostCameraInfo* in)
{
    if (!in) return;
    auto clampf = [](float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); };
    for (GhostCamera& c : g_scene.cameras)
        if (c.id == in->id)
        {
            c.position = { in->px, in->py, in->pz };
            c.yawDeg = in->yawDeg;
            c.pitchDeg = clampf(in->pitchDeg, -88.0f, 88.0f);
            c.fovDeg = clampf(in->fovDeg, 10.0f, 120.0f);
            if (in->resW <= 0 || in->resH <= 0) { c.resW = 0; c.resH = 0; }
            else { c.resW = (int)clampf((float)in->resW, 16.0f, 4096.0f); c.resH = (int)clampf((float)in->resH, 16.0f, 4096.0f); }

            c.mode = in->mode == 1 ? 1 : 0;
            c.targetId = in->targetId > 0 ? in->targetId : 0;
            c.minDist = clampf(in->minDist, 0.3f, 100.0f);
            c.maxDist = clampf(in->maxDist, c.minDist, 100.0f);
            c.distance = clampf(in->distance, c.minDist, c.maxDist);
            c.heightOffset = clampf(in->heightOffset, -20.0f, 20.0f);
            c.minPitchDeg = clampf(in->minPitchDeg, -88.0f, 88.0f);
            c.maxPitchDeg = clampf(in->maxPitchDeg, c.minPitchDeg, 88.0f);
            c.smoothing = clampf(in->smoothing, 0.0f, 50.0f);
            c.moveSpeed = clampf(in->moveSpeed, 0.1f, 200.0f);
            return;
        }
}

void GhostEngine_SetActiveCamera(int id)
{
    for (const GhostCamera& c : g_scene.cameras)
        if (c.id == id)
        {
            if (g_scene.activeCameraId != id) { g_scene.activeCameraId = id; g_scene.version++; }
            return;
        }
}

int GhostEngine_GetActiveCamera() { return g_scene.activeCameraId; }


// ============================================================
//  Exported: video
// ============================================================
static int BuildResolutionList(int* out, int maxPairs)
{
    std::vector<std::pair<int, int>> v;
    for (int i = 0; i < GHOST_RES_PRESET_COUNT; i++) v.push_back({ kGhostResPresets[i][0], kGhostResPresets[i][1] });

    unsigned int modes[256 * 2];
    const int n = g_rhi ? g_rhi->GetDisplayModes(modes, 256) : 0;
    for (int i = 0; i < n; i++) v.push_back({ (int)modes[i * 2], (int)modes[i * 2 + 1] });

    v.erase(std::remove_if(v.begin(), v.end(), [](const std::pair<int, int>& s)
        { return s.first < GHOST_VIDEO_MIN_DIM || s.second < GHOST_VIDEO_MIN_DIM ||
        s.first > GHOST_VIDEO_MAX_DIM || s.second > GHOST_VIDEO_MAX_DIM; }), v.end());
    std::sort(v.begin(), v.end(), [](const std::pair<int, int>& a, const std::pair<int, int>& b)
        { const long long pa = (long long)a.first * a.second, pb = (long long)b.first * b.second;
    return pa != pb ? pa < pb : a.first < b.first; });
    v.erase(std::unique(v.begin(), v.end()), v.end());

    int count = 0;
    if (out) for (const auto& s : v) { if (count >= maxPairs) break; out[count * 2] = s.first; out[count * 2 + 1] = s.second; count++; }
    return count;
}

void GhostEngine_GetVideoSettings(GhostVideoSettings* out) { if (out) *out = g_video; }

void GhostEngine_SetVideoSettings(const GhostVideoSettings* in)
{
    if (!in) return;
    GhostVideoSettings s = *in;
    GhostVideo_Sanitize(&s);
    g_video = s;                     // the form's video timer sees the change and applies the window part
}

void GhostEngine_GetResolution(int* w, int* h) { if (w) *w = g_mainRW; if (h) *h = g_mainRH; }

int GhostEngine_GetResolutionList(int* outWH, int maxPairs)
{
    if (!outWH || maxPairs <= 0) return 0;
    return BuildResolutionList(outWH, maxPairs);
}

// ============================================================
//  Exported: viewports
// ============================================================
int GhostEngine_CreateViewport(HWND hwnd, unsigned int width, unsigned int height, int cameraId)
{
    if (!g_rhi || !g_pipeline || !FindCamera(cameraId)) return 0;
    if (width == 0) width = 1;
    if (height == 0) height = 1;

    int id = g_rhi->CreateSurface(hwnd, width, height);
    if (id <= 0) return 0;
    if (GhostResizeDepthBuffer(g_rhi, g_pipeline, id, width, height) != GHOST_OK)
    {
        g_rhi->DestroySurface(id);
        return 0;
    }
    g_vp[id].alive = true;
    g_vp[id].visible = true;
    g_vp[id].cameraId = cameraId;
    return id;
}

void GhostEngine_DestroyViewport(int vp)
{
    if (!g_rhi || !g_pipeline || vp <= 0 || vp >= GHOST_MAX_SURFACES) return;
    g_rhi->DestroySurface(vp);
    GhostReleaseDepth(g_pipeline, vp);
    GhostReleaseOffscreen(g_pipeline, vp);
    GhostMSAA_Release(g_msaa, vp);
    GhostTAA_Release(g_taa, vp);
    g_vp[vp] = EngineViewport();
}

int GhostEngine_RebindViewport(int vp, HWND hwnd, unsigned int width, unsigned int height)
{
    if (!g_rhi || !g_pipeline || vp < 0 || vp >= GHOST_MAX_SURFACES || !g_vp[vp].alive) return 0;
    if (width == 0) width = 1;
    if (height == 0) height = 1;
    if (g_rhi->RebindSurface(vp, hwnd, width, height) != GHOST_OK) return 0;
    GhostResizeDepthBuffer(g_rhi, g_pipeline, vp, width, height);
    if (vp == 0) { g_width = width; g_height = height; }
    return 1;
}

void GhostEngine_ResizeViewport(int vp, unsigned int width, unsigned int height)
{
    if (vp == 0) { GhostEngine_Resize(width, height); return; }
    if (!g_rhi || !g_pipeline || vp < 0 || vp >= GHOST_MAX_SURFACES || !g_vp[vp].alive) return;
    if (width == 0 || height == 0) return;
    g_rhi->ResizeSurface(vp, width, height);

  //  GhostResizeDepthBuffer(g_rhi, g_pipeline, vp, width, height);
}

void GhostEngine_SetViewportVisible(int vp, int visible)
{
    if (vp <= 0 || vp >= GHOST_MAX_SURFACES) return;       // the main view is always on
    g_vp[vp].visible = (visible != 0);
}

void GhostEngine_SetInputViewport(int vp)
{
    if (vp < 0 || vp >= GHOST_MAX_SURFACES || !g_vp[vp].alive) { g_inputEnabled = false; return; }
    g_inputEnabled = true;
    if (GhostCamera* c = ViewportCamera(vp)) g_inputCameraId = c->id;
}

// ============================================================
//  Exported: scene files
// ============================================================


static void CopyMsg(const std::string& m, char* buf, int size)
{
    if (buf && size > 0) strncpy_s(buf, size, m.c_str(), _TRUNCATE);
}
void GhostEngine_SetLoadProgress(GhostLoadProgressFn fn) { g_loadProgress = fn; }
// Used by the scene loader: reuse a texture that is already on the GPU, otherwise decode and upload it
static int LoadSceneTexture(const std::string& absPath, const std::string& name)
{
    LoadNote("Texture", absPath.substr(absPath.find_last_of("\\/") == std::string::npos ? 0 : absPath.find_last_of("\\/") + 1));
    for (const GhostTextureInfo& t : g_scene.textures)
        if (t.id >= GHOST_FIRST_USER_TEXTURE && !t.path.empty() && _stricmp(t.path.c_str(), absPath.c_str()) == 0)
            return t.id;

    std::vector<unsigned char> px;
    unsigned int w = 0, h = 0;
    if (!GhostImage_Load(absPath, 4096, px, w, h)) return 0;

    int id = RegisterTexture(name.c_str(), px.data(), w, h);
    if (id < 0) return 0;
    g_scene.textures[id].path = absPath;
    return id;
}

void GhostEngine_SetProjectRoot(const char* path)
{
    g_projectRoot = GhostPath_Normalize(path ? path : "");
    GhostAudioBank_Load(g_projectRoot);
}

int GhostEngine_SaveScene(const char* path, char* msg, int msgSize)
{
    GhostEngine_Stop();       // saving always works on the real scene, never the play copy

    std::string err;
    if (!path || !GhostScene_Save(g_scene, g_projectRoot, path, err))
    {
        CopyMsg(err.empty() ? "Save failed." : err, msg, msgSize);
        return 0;
    }
    g_scenePath = GhostPath_Normalize(path);
    return 1;
}

int GhostEngine_LoadScene(const char* path, char* msg, int msgSize)
{
    if (!g_rhi || !g_pipeline || !path) { CopyMsg("The engine is not ready.", msg, msgSize); return 0; }
  
    GhostEngine_Stop();
    LoadNote("Scene", path);
    g_modelWarn.clear();
    g_animRt.clear();
    GhostScene backup = g_scene;
    std::string err, warn;
    bool ok = GhostScene_Load(g_scene, g_projectRoot, path, LoadSceneTexture,
        MAX_SCENE_OBJECT_COUNT, err, warn, LoadModelFile);
    if (!ok)
    {
        // Restore everything except the texture registry: textures loaded during the attempt stay on the GPU
        std::vector<GhostTextureInfo> tex = g_scene.textures;
        g_scene = backup;
        g_scene.textures = tex;
        CopyMsg(err, msg, msgSize);
        return 0;
    }
    warn += g_modelWarn;
    g_hoverId = 0; g_dragging = false; g_dragId = 0; g_inputCameraId = 0;
    FreeAllTileInstances();
    g_scenePath = GhostPath_Normalize(path);
    g_scene.version++;
    UndoReset();
    CopyMsg(warn, msg, msgSize);
    return warn.empty() ? 1 : 2;
}

void GhostEngine_ClearScene()
{
    GhostEngine_Stop();
    g_animRt.clear();
    g_scene.objects.clear(); g_scenePath.clear();
    FreeAllTileInstances();
    g_scene.lights.clear();
    g_scene.cameras.clear(); g_scene.mods.clear();
    g_scene.ambientColor[0] = g_scene.ambientColor[1] = g_scene.ambientColor[2] = 1.0f;
    g_scene.ambientIntensity = 0.15f;    g_scene.shadowsEnabled = true;
    g_scene.shadowStrength = 1.0f;
    g_scene.shadowRes = 1024;
    g_scene.shadowFilter = 2; g_scene.shadowSoftness = 1.0f; g_scene.shadowBias = 1.0f;
    g_scene.shadowNormalBias = 1.0f; g_scene.shadowDistance = 30.0f;
    g_scene.nextId = 1;
    g_scene.music.clear(); g_scene.ambience.clear();
    g_scene.selectedId = 0;
    g_scene.selectedFace = -1;
    g_scene.activeCameraId = 0;
    GhostScene_AddCamera(g_scene);            // the default camera, id 1
    g_hoverId = 0; g_dragging = false; g_dragId = 0; g_inputCameraId = 0;
    g_scene.version++;
    UndoReset();
}

unsigned int GhostEngine_GetSceneHash()
{
    return GhostScene_Hash(g_playState != GhostPlayState::Editing ? g_playBackup : g_scene);
}

void GhostEngine_SetTexturePath(int id, const char* path)
{
    if (id < GHOST_FIRST_USER_TEXTURE || id >= (int)g_scene.textures.size() || !path) return;
    g_scene.textures[id].path = GhostPath_Normalize(path);
}

int GhostEngine_GetTexturePath(int id, char* buf, int bufSize)
{
    if (!buf || bufSize <= 0) return 0;
    buf[0] = 0;
    if (id < 0 || id >= (int)g_scene.textures.size()) return 0;
    CopyMsg(g_scene.textures[id].path, buf, bufSize);
    return g_scene.textures[id].path.empty() ? 0 : 1;
}

// ============================================================
//  Game API: what GhostGame.dll receives (see GhostGameAPI.h)
// ============================================================
static void   Api_Log(const char* m) { OutputDebugStringA("[Game] "); OutputDebugStringA(m ? m : ""); OutputDebugStringA("\n"); }
static double Api_GetTime() { return g_playTime; }
static float  Api_GetDt() { return GHOST_FIXED_DT; }
static void   Api_SetHud(const char* t) { g_hudText = t ? t : ""; if (g_hudText.size() > 250) g_hudText.resize(250); }

static int Api_KeyDown(int vk) { return (vk > 0 && vk < 256 && g_input.down[vk]) ? 1 : 0; }
static int Api_KeyPressed(int vk) { return (vk > 0 && vk < 256 && g_input.pressed[vk]) ? 1 : 0; }
static int Api_KeyReleased(int vk) { return (vk > 0 && vk < 256 && g_input.released[vk]) ? 1 : 0; }

// is this correct palceemnts?
static void  Api_GetMouseDelta(float* v) { if (v) { v[0] = g_apiMouseDX; v[1] = g_apiMouseDY; } }
static float Api_GetMouseWheel() { return g_apiWheel; }
static int   Api_GetCameraMode(int id) { GhostCamera* c = FindCamera(id); return c ? c->mode : -1; }
static int   Api_GetCameraTarget(int id) { GhostCamera* c = FindCamera(id); return c ? c->targetId : 0; }
static int   Api_SetCameraTarget(int id, int obj)
{
    GhostCamera* c = FindCamera(id);
    if (!c || (obj != 0 && !FindObject(obj))) return 0;
    c->targetId = obj;
    c->orbitValid = false;
    return 1;
}
// end

static int Api_FindByTag(const char* tag)
{
    if (!tag) return 0;
    for (const GhostObject& o : g_scene.objects) if (GhostObject_Tag(o) == tag) return o.id;
    return 0;
}

static int Api_FindAllByTag(const char* tag, int* out, int maxCount)
{
    if (!tag || !out || maxCount <= 0) return 0;
    int n = 0;
    for (const GhostObject& o : g_scene.objects)
        if (GhostObject_Tag(o) == tag) { out[n++] = o.id; if (n >= maxCount) break; }
    return n;
}

static int Api_GetTag(int id, char* buf, int size)
{
    GhostObject* o = FindObject(id);
    if (!o || !buf || size <= 0) return 0;
    CopyMsg(GhostObject_Tag(*o), buf, size);
    return 1;
}

static int Api_SetTag(int id, const char* tag)
{
    GhostObject* o = FindObject(id);
    if (!o) return 0;
    std::string t = tag ? tag : "";
    static const char* names[GHOST_ROLE_COUNT] = { "", "Player", "Enemy", "Coin", "Block", "Goal", nullptr,
        "PortalEntry", "PortalExit", "Breakable", "Mover", "Projectile", "Boss", "HiddenBlock" };
    o->role = GHOST_ROLE_CUSTOM;
    for (int i = 0; i < GHOST_ROLE_COUNT; i++) if (names[i] && t == names[i]) { o->role = i; break; }
    if (o->role == GHOST_ROLE_CUSTOM) { if (t.size() > 31) t.resize(31); o->customTag = t; }
    return 1;
}

static int Api_GetPosition(int id, float* v)
{
    GhostObject* o = FindObject(id); if (!o || !v) return 0; v[0] = o->position.x; v[1] = o->position.y; v[2] = o->position.z; return 1;
}
static int Api_SetPosition(int id, const float* v)
{
    GhostObject* o = FindObject(id); if (!o || !v) return 0; o->position = { v[0], v[1], v[2] }; return 1;
}
static int Api_GetRotation(int id, float* v)
{
    GhostObject* o = FindObject(id); if (!o || !v) return 0; v[0] = o->rotationDeg.x; v[1] = o->rotationDeg.y; v[2] = o->rotationDeg.z; return 1;
}
static int Api_SetRotation(int id, const float* v)
{
    GhostObject* o = FindObject(id); if (!o || !v) return 0; o->rotationDeg = { v[0], v[1], v[2] }; return 1;
}
static int Api_GetScale(int id, float* v)
{
    GhostObject* o = FindObject(id); if (!o || !v) return 0; v[0] = o->scale.x; v[1] = o->scale.y; v[2] = o->scale.z; return 1;
}
static int Api_SetScale(int id, const float* v)
{
    GhostObject* o = FindObject(id);
    if (!o || !v) return 0;
    auto sc = [](float s) { return fabsf(s) < 0.001f ? 0.001f : s; };
    o->scale = { sc(v[0]), sc(v[1]), sc(v[2]) };
    return 1;
}

static int Api_GetColor(int id, float* v)
{
    GhostObject* o = FindObject(id);
    if (!o || !v) return 0;
    const float* c = (o->meshId == GHOST_MESH_QUAD) ? o->sprite.tint : o->faces[0].tint;
    v[0] = c[0]; v[1] = c[1]; v[2] = c[2];
    return 1;
}
static int Api_SetColor(int id, const float* v)
{
    GhostObject* o = FindObject(id);
    if (!o || !v) return 0;
    auto c01 = [](float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); };
    for (GhostMaterial& m : o->faces) { m.tint[0] = c01(v[0]); m.tint[1] = c01(v[1]); m.tint[2] = c01(v[2]); }
    if (o->meshId == GHOST_MESH_QUAD) for (int i = 0; i < 3; i++) o->sprite.tint[i] = c01(v[i]);
    o->useVertexColor = false;
    return 1;
}
static int Api_GetVisible(int id) { GhostObject* o = FindObject(id); return (o && o->visible) ? 1 : 0; }
static int Api_SetVisible(int id, int vis) { GhostObject* o = FindObject(id); if (!o) return 0; o->visible = (vis != 0); return 1; }

static bool ColliderBounds(const GhostObject& o, GhostVec3& mn, GhostVec3& mx)
{
    if (!o.collider) return false;
    GhostVec3 h = { fabsf(o.colliderSize.x * o.scale.x) * 0.5f,
                    fabsf(o.colliderSize.y * o.scale.y) * 0.5f,
                    fabsf(o.colliderSize.z * o.scale.z) * 0.5f };
    GhostVec3 c = { o.position.x + o.colliderOffset.x * o.scale.x,
                    o.position.y + o.colliderOffset.y * o.scale.y,
                    o.position.z + o.colliderOffset.z * o.scale.z };
    GhostVec3 ax[3];
    if (ColliderAxes(o, ax))                              // enclosing axis-aligned box of the rotated one
        h = GhostVec3(fabsf(ax[0].x) * h.x + fabsf(ax[1].x) * h.y + fabsf(ax[2].x) * h.z,
            fabsf(ax[0].y) * h.x + fabsf(ax[1].y) * h.y + fabsf(ax[2].y) * h.z,
            fabsf(ax[0].z) * h.x + fabsf(ax[1].z) * h.y + fabsf(ax[2].z) * h.z);
    mn = c - h;
    mx = c + h;
    return true;
}
static int Api_GetCollider(int id, float* c3, float* h3, float* ax9)
{
    GhostObject* o = FindObject(id);
    if (!o || !o->collider) return 0;
    GhostVec3 ax[3];
    const bool rot = ColliderAxes(*o, ax);
    if (c3) { c3[0] = o->position.x + o->colliderOffset.x * o->scale.x; c3[1] = o->position.y + o->colliderOffset.y * o->scale.y; c3[2] = o->position.z + o->colliderOffset.z * o->scale.z; }
    if (h3) { h3[0] = fabsf(o->colliderSize.x * o->scale.x) * 0.5f; h3[1] = fabsf(o->colliderSize.y * o->scale.y) * 0.5f; h3[2] = fabsf(o->colliderSize.z * o->scale.z) * 0.5f; }
    if (ax9) for (int k = 0; k < 3; k++) { ax9[k * 3] = ax[k].x; ax9[k * 3 + 1] = ax[k].y; ax9[k * 3 + 2] = ax[k].z; }
    return rot ? 2 : 1;
}
static int Api_GetBounds(int id, float* mn3, float* mx3)
{
    GhostObject* o = FindObject(id);
    GhostVec3 mn, mx;
    if (!o || !mn3 || !mx3 || !ColliderBounds(*o, mn, mx)) return 0;
    mn3[0] = mn.x; mn3[1] = mn.y; mn3[2] = mn.z;
    mx3[0] = mx.x; mx3[1] = mx.y; mx3[2] = mx.z;
    return 1;
}

static int Api_QueryBox(const float* bmin, const float* bmax, int* out, int maxCount)
{
    if (!bmin || !bmax || !out || maxCount <= 0) return 0;
    int n = 0;
    for (const GhostObject& o : g_scene.objects)
    {
        if (!o.visible) continue;
        GhostVec3 mn, mx;
        if (!ColliderBounds(o, mn, mx)) continue;
        if (mn.x < bmax[0] && mx.x > bmin[0] && mn.y < bmax[1] && mx.y > bmin[1] && mn.z < bmax[2] && mx.z > bmin[2])
        {
            out[n++] = o.id;
            if (n >= maxCount) break;
        }
    }
    return n;
}

static int Api_IsTerrain(int id)
{
    GhostObject* o = FindObject(id);
    return (o && o->tileMap.enabled && o->tileMap.direction == 1) ? 1 : 0;   // only floor maps are height fields
}

// World height of a floor Tile Map (direction Y) under world (x, z). 0 = not a terrain / outside its grid.
// Position, scale and Y rotation are honoured. X / Z tilt is approximate.
static int Api_GetTerrainHeight(int id, float wx, float wz, float* outY)
{
    GhostObject* o = FindObject(id);
    if (!o || !outY || !o->tileMap.enabled || o->tileMap.direction != 1) return 0;
    const GhostTileMap& t = o->tileMap;
    const GhostVec3 lp = XformPoint(ObjectInverse(*o), GhostVec3(wx, o->position.y, wz), 1.0f);
    const float hu = t.gridX * t.tileSize * 0.5f, hv = t.gridY * t.tileSize * 0.5f;
    if (lp.x < -hu || lp.x > hu || lp.z < -hv || lp.z > hv) return 0;
    const float h = GhostTileMap_HeightMesh(t, lp.x, lp.z);
    *outY = XformPoint(GhostObjectModel(*o), GhostVec3(lp.x, h, lp.z), 1.0f).y;
    return 1;
}

static int Api_Spawn(int meshId, const float* pos)
{
    if (!CanAddObject(meshId)) return 0;
    GhostObject* o = GhostScene_AddObject(g_scene, meshId);
    if (!o) return 0;
    if (pos) o->position = { pos[0], pos[1], pos[2] };
    return o->id;
}

static int Api_Destroy(int id)
{
    if (!FindObject(id)) return 0;                     // objects only: lights and cameras can't be destroyed from here
    return GhostScene_Remove(g_scene, id) ? 1 : 0;
}

static int Api_GetActiveCamera() { return g_scene.activeCameraId; }
static int Api_SetActiveCamera(int id)
{
    if (!FindCamera(id)) return 0;
    GhostEngine_SetActiveCamera(id);
    return 1;
}

static int Api_GetCamera(int id, float* pos, float* yp, float* fov)
{
    GhostCamera* c = FindCamera(id);
    if (!c) return 0;
    if (pos) { pos[0] = c->position.x; pos[1] = c->position.y; pos[2] = c->position.z; }
    if (yp) { yp[0] = c->yawDeg; yp[1] = c->pitchDeg; }
    if (fov) *fov = c->fovDeg;
    return 1;
}

static int Api_SetCamera(int id, const float* pos, const float* yp, const float* fov)
{
    GhostCamera* c = FindCamera(id);
    if (!c) return 0;
    if (pos) c->position = { pos[0], pos[1], pos[2] };
    if (yp) { c->yawDeg = yp[0]; c->pitchDeg = yp[1] < -88.0f ? -88.0f : (yp[1] > 88.0f ? 88.0f : yp[1]); }
    if (fov) c->fovDeg = *fov < 10.0f ? 10.0f : (*fov > 120.0f ? 120.0f : *fov);
    return 1;
}
static const float kUIWhite[4] = { 1, 1, 1, 1 };

static void PushQuad(int tex, float x, float y, float w, float h, const float* c)
{
    if (g_uiQuads.size() >= GHOST_UI_MAX_QUADS) return;
    if (!c) c = kUIWhite;
    GhostUIQuad q;
    q.x = x; q.y = y; q.w = w; q.h = h;
    q.u0 = 0.0f; q.v0 = 0.0f; q.u1 = 1.0f; q.v1 = 1.0f;
    q.color[0] = c[0]; q.color[1] = c[1]; q.color[2] = c[2]; q.color[3] = c[3];
    q.textureId = tex;
    g_uiQuads.push_back(q);
}

static void  Api_UIRect(float x, float y, float w, float h, const float* c) { PushQuad(0, x, y, w, h, c); }
static void  Api_UIImage(int tex, float x, float y, float w, float h, const float* c) { PushQuad(tex, x, y, w, h, c); }
static void  Api_UIText(float x, float y, float size, const char* t, const float* c)
{
    if (!t) return;
    GhostText_Draw(g_uiQuads, g_font, x, y, size, t, c ? c : kUIWhite);
}
static float Api_UIMeasure(const char* t, float size) { return GhostText_Measure(g_font, size, t); }
static void  Api_UIScreenSize(float* wh) { if (wh) { wh[0] = g_uiRefW; wh[1] = 1080.0f; } }

static int Api_LoadTexture(const char* rel)
{
    if (!rel || !*rel) return 0;
    std::string p = rel;
    const bool absolute = p.size() > 1 && (p[1] == ':' || p[0] == '\\' || p[0] == '/');
    if (!absolute && !g_projectRoot.empty()) p = g_projectRoot + "\\" + p;
    p = GhostPath_Normalize(p);
    const size_t sl = p.find_last_of("\\/");
    return LoadSceneTexture(p, sl == std::string::npos ? p : p.substr(sl + 1));
}

static GhostUIAsset* UiAsset(int h)
{
    return (h >= 1 && h <= (int)g_uiAssets.size()) ? &g_uiAssets[(size_t)h - 1] : nullptr;
}

static GhostWidget* UiWidget(int h, const char* name)
{
    GhostUIAsset* a = UiAsset(h);
    if (!a || !name) return nullptr;
    for (GhostWidget& w : a->widgets) if (w.name == name) return &w;
    return nullptr;
}

static int ResolveFont(const std::string& stored)
{
    if (stored.empty() || !g_fontProvider) return 0;
    const std::string abs = StoredToAbs(stored);
    for (auto& kv : g_fontCache) if (_stricmp(kv.first.c_str(), abs.c_str()) == 0) return kv.second;
    const int slot = g_fontProvider(abs.c_str());
    g_fontCache.push_back({ abs, slot });
    return slot;
}

static int Api_UILoad(const char* rel)
{
    if (!rel || !*rel) return 0;
    std::string p = rel;
    const bool absolute = p.size() > 1 && (p[1] == ':' || p[0] == '\\' || p[0] == '/');
    if (!absolute && !g_projectRoot.empty()) p = g_projectRoot + "\\" + p;
    p = GhostPath_Normalize(p);

    for (size_t i = 0; i < g_uiAssets.size(); i++)                        // the same file twice = the same handle
        if (_stricmp(g_uiAssets[i].path.c_str(), p.c_str()) == 0) return (int)i + 1;
    if (g_uiAssets.size() >= 16) return 0;

    GhostUIAsset a;
    std::string err, warn;
    if (!GhostUI_Load(a, g_projectRoot, p, LoadSceneTexture, err, warn))
    {
        Api_Log(("UILoad failed: " + err).c_str());
        return 0;
    }
    if (!warn.empty()) Api_Log(("UILoad: " + warn).c_str());
    for (GhostWidget& w : a.widgets) w.fontId = ResolveFont(w.font);
    a.path = p;
    g_uiAssets.push_back(std::move(a));
    return (int)g_uiAssets.size();
}

static void Api_UIShow(int h, int vis) { if (GhostUIAsset* a = UiAsset(h)) a->shown = (vis != 0); }

static int Api_UISetText(int h, const char* name, const char* t)
{
    GhostWidget* w = UiWidget(h, name);
    if (!w) return 0;
    std::string s = t ? t : "";
    if (s.size() > 255) s.resize(255);
    w->text = s;
    return 1;
}

static int Api_UISetProgress(int h, const char* name, float v)
{
    GhostWidget* w = UiWidget(h, name);
    if (!w) return 0;
    w->value = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
    return 1;
}

static int Api_UISetVisible(int h, const char* name, int vis)
{
    GhostWidget* w = UiWidget(h, name);
    if (!w) return 0;
    w->visible = (vis != 0);
    return 1;
}

static int Api_UISetColor(int h, const char* name, const float* c)
{
    GhostWidget* w = UiWidget(h, name);
    if (!w || !c) return 0;
    for (int i = 0; i < 4; i++) w->color[i] = c[i];
    return 1;
}

static int Api_UIButtonClicked(int h, const char* name)
{
    GhostWidget* w = UiWidget(h, name);
    if (!w || !w->clicked) return 0;
    w->clicked = false;
    return 1;
}

static int Api_UIButtonDown(int h, const char* name)
{
    GhostWidget* w = UiWidget(h, name);
    return (w && w->pressed) ? 1 : 0;
}

static int Api_UIGetMouse(float* xy)
{
    if (xy) { xy[0] = g_uiMouseX; xy[1] = g_uiMouseY; }
    return g_uiMouseInside ? 1 : 0;
}
static float Api_SkyGetTime()
{
    for (const GhostModEntity& m : g_scene.mods) if (m.typeId == GHOST_MOD_SKY) return GhostSky_GetTime(m);
    return 0.0f;
}
static void Api_SkySetTime(float h)
{
    for (GhostModEntity& m : g_scene.mods) if (m.typeId == GHOST_MOD_SKY) { GhostSky_SetTime(m, h); return; }
}

// ============================================================
//  Sprites and animations
// ============================================================
static std::string StoredToAbs(const std::string& stored)
{
    std::string p = stored;
    const bool absolute = p.size() > 1 && (p[1] == ':' || p[0] == '\\' || p[0] == '/');
    if (!absolute && !g_projectRoot.empty()) p = g_projectRoot + "\\" + p;
    return GhostPath_Normalize(p);
}

static int LoadAnimFile(const std::string& stored)
{
    const std::string abs = StoredToAbs(stored);
    LoadNote("Animation", stored);
    for (size_t i = 0; i < g_anims.size(); i++)
        if (_stricmp(g_anims[i].path.c_str(), abs.c_str()) == 0) return (int)i;
    if (g_anims.size() >= 64) return -1;

    GhostAnimAsset a;
    std::string err, warn;
    if (!GhostAnim_Load(a, g_projectRoot, abs, LoadSceneTexture, err, warn))
    {
        OutputDebugStringA(("[GhostX] Animation load failed: " + err + "\n").c_str());
        return -1;
    }
    if (!warn.empty()) OutputDebugStringA(("[GhostX] Animation: " + warn).c_str());
    a.path = abs;
    g_anims.push_back(std::move(a));
    return (int)g_anims.size() - 1;
}

// Re-reads every loaded .ghostanim (ids stay the same). Runs at every Play, and from Play > Reload Animations.
static void ReloadAnims()
{
    for (GhostAnimAsset& a : g_anims)
    {
        GhostAnimAsset n;
        std::string err, warn;
        if (GhostAnim_Load(n, g_projectRoot, a.path, LoadSceneTexture, err, warn)) { n.path = a.path; a = std::move(n); }
        else OutputDebugStringA(("[GhostX] Animation reload failed: " + err + "\n").c_str());
    }
    // 3D clips: drop the cached FBX clips that changed on disk (the rest stay loaded)
    for (auto it = g_skelClips.begin(); it != g_skelClips.end(); )
    {
        std::error_code ec;
        const auto mt = std::filesystem::last_write_time(std::filesystem::path(GhostUtf8ToWide(it->first)), ec);
        if (ec || mt != it->second.mtime) it = g_skelClips.erase(it); else ++it;
    }
}

// Replace the existing SpriteDrawInfo function in GhostEngine.cpp with this:

static void SpriteDrawInfo(const GhostObject& o, int& tex, int& cols, int& rows, int& frame)
{
    const GhostSprite& s = o.sprite;
    tex = s.textureId;
    cols = s.source == 0 ? 1 : s.cols;
    rows = s.source == 0 ? 1 : s.rows;

    if (s.animId >= 0 && s.animId < (int)g_anims.size())
    {
        const GhostAnimAsset& a = g_anims[(size_t)s.animId];

        // Find the active clip — same logic as UpdateSprites
        int ci = GhostAnim_FindClip(a, s.clip.c_str());
        if (ci < 0) ci = 0;

        if (ci < (int)a.clips.size())
        {
            const GhostClip& c = a.clips[(size_t)ci];
            if (c.textureId > 0) tex = c.textureId;
            cols = c.source == 0 ? 1 : c.cols;
            rows = c.source == 0 ? 1 : c.rows;
        }
    }

    if (cols < 1) cols = 1;
    if (rows < 1) rows = 1;
    frame = s.curFrame < 0 ? 0 : (s.curFrame > cols * rows - 1 ? cols * rows - 1 : s.curFrame);
}

static void UpdateSprites(float dt)
{
    const bool paused = g_playState == GhostPlayState::Paused;
    for (GhostObject& o : g_scene.objects)
    {
        if (o.meshId != GHOST_MESH_QUAD) continue;
        GhostSprite& s = o.sprite;
        if (!s.animResolved)
        {
            s.animResolved = true;
            s.animId = s.anim.empty() ? -1 : LoadAnimFile(s.anim);
        }

        const GhostAnimAsset* a = (s.animId >= 0 && s.animId < (int)g_anims.size()) ? &g_anims[(size_t)s.animId] : nullptr;
        if (a && !a->clips.empty())
        {
            int ci = GhostAnim_FindClip(*a, s.clip.c_str());
            if (ci < 0) ci = 0;                                    // unknown or empty clip name: the first clip
            const GhostClip& c = a->clips[(size_t)ci];
            if (!paused && s.playing) s.animTime += dt * s.animSpeed;
            const float dur = (float)c.frames.size() / c.fps;
            if (c.loop && dur > 0.0f && s.animTime > dur) s.animTime = fmodf(s.animTime, dur);
            s.curFrame = GhostAnim_FrameAt(c, s.animTime);
        }
        else s.curFrame = s.frame;
    }
}

// ---- 3D clips ----
static std::shared_ptr<GhostSkelClip> GetSkelClip(const std::string& stored)
{
    if (stored.empty()) return nullptr;
    const std::string abs = StoredToAbs(stored);
    auto it = g_skelClips.find(abs);
    if (it != g_skelClips.end()) return it->second.clip;

    SkelClipEntry e;
    auto c = std::make_shared<GhostSkelClip>();
    std::string err;
    std::error_code ec;
    e.mtime = std::filesystem::last_write_time(std::filesystem::path(GhostUtf8ToWide(abs)), ec);
    if (GhostSkelClip_Load(abs, *c, err)) e.clip = c;
    else OutputDebugStringA(("[GhostX] 3D animation load failed: " + abs + "\n" + err + "\n").c_str());
    g_skelClips[abs] = e;                       // a failed load is remembered too, so it isn't retried every frame
    return e.clip;
}

// The clip a model plays when none is chosen: Idle, else the first one
static int DefaultClipIndex(const GhostAnimAsset& a)
{
    int ci = GhostAnim_FindClip(a, "Idle");
    if (ci < 0)
        for (const auto& kv : a.states)
            if (_stricmp(kv.first.c_str(), "Idle") == 0) { ci = GhostAnim_FindClip(a, kv.second.c_str()); break; }
    return ci < 0 ? 0 : ci;
}


static float Ease(int curve, float t)
{
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    switch (curve) { case 1: return t * t; case 2: return 1.0f - (1.0f - t) * (1.0f - t); case 3: return t * t * (3.0f - 2.0f * t); default: return t; }
}
static float WrapDeg(float a) { while (a > 180.0f) a -= 360.0f; while (a < -180.0f) a += 360.0f; return a; }

// dst = pose of the new clip, src = pose of the old clip. w 0 = old, 1 = new.
static void BlendVerts(GhostMeshVertex* dst, const GhostMeshVertex* src, float w, size_t count)
{
    const size_t stride = sizeof(GhostMeshVertex) / sizeof(float);
    float* d = reinterpret_cast<float*>(dst);
    const float* s = reinterpret_cast<const float*>(src);
    for (size_t i = 0; i < count; i++, d += stride, s += stride)
    {
        static const int K[6] = { 0, 1, 2, 7, 8, 9 };                    // position + normal
        for (int k : K) d[k] = s[k] + (d[k] - s[k]) * w;
        const float len = sqrtf(d[7] * d[7] + d[8] * d[8] + d[9] * d[9]);
        if (len > 1e-6f) { d[7] /= len; d[8] /= len; d[9] /= len; }
    }
}

// Global modifiers (Turn Smoothing, Lean) and the squash pulses
static void UpdateAnimModifiers(GhostObject& o, const GhostAnimAsset& a, AnimRt& r, float dt, bool live)
{
    GhostAnimator& an = o.animator;
    const GhostGraphNode* turn = nullptr; const GhostGraphNode* lean = nullptr;
    for (const GhostGraphNode& n : a.graph.nodes)
    {
        if (n.type == GNODE_TURN && !turn) turn = &n;
        if (n.type == GNODE_LEAN && !lean) lean = &n;
    }

    float off = 0.0f;
    if (turn && live)
    {
        const float yaw = o.rotationDeg.y;
        if (!r.haveYaw) { r.visYaw = yaw; r.haveYaw = true; }
        off = WrapDeg(r.visYaw - yaw);                         // what the game just snapped
        off *= expf(-dt * 4.0f / fmaxf(turn->p[0], 0.02f));    // p[0] = time to settle
        if (fabsf(off) < 0.05f) off = 0.0f;
        r.visYaw = yaw + off;
    }
    else r.haveYaw = false;
    an.yawOffset = off;

    float target = 0.0f;
    if (lean && live)
    {
        target = off * lean->p[1];
        const float mx = fmaxf(lean->p[0], 0.0f);
        target = target < -mx ? -mx : (target > mx ? mx : target);
    }
    r.lean += (target - r.lean) * (1.0f - expf(-dt * 12.0f));
    an.leanDeg = r.lean;

    float sy = 1.0f;
    for (AnimFx& f : r.fx) { f.t += dt; if (f.t < f.dur) sy *= 1.0f + f.amt * sinf(GHOST_PI * f.t / f.dur); }
    r.fx.erase(std::remove_if(r.fx.begin(), r.fx.end(), [](const AnimFx& f) { return f.t >= f.dur; }), r.fx.end());
    sy = sy < 0.3f ? 0.3f : (sy > 3.0f ? 3.0f : sy);
    const float sxz = 1.0f / sqrtf(sy);                         // keeps the volume
    an.scaleMul = { sxz, sy, sxz };
}

static void UpdateAnimators(float dt)
{
    if (!g_rhi || !g_pipeline) return;
    const bool editing = g_playState == GhostPlayState::Editing;
    const bool playing = g_playState == GhostPlayState::Playing;
    const bool paused = g_playState == GhostPlayState::Paused;
    for (auto& kv : g_skinInst) kv.second.seen = false;
    std::set<int> animated;

    if (editing && g_prevstate.active && g_prevstate.playing) g_prevstate.time += dt * g_prevstate.speed;

    for (GhostObject& o : g_scene.objects)
    {
        GhostAnimator& an = o.animator;
        if (o.meshId == GHOST_MESH_QUAD) continue;
        auto skIt = g_skinByMesh.find(o.meshId);
        if (skIt == g_skinByMesh.end()) continue;
        const GhostSkinData& sk = *skIt->second;
        const bool isPrev = editing && g_prevstate.active && o.id == g_prevstate.objectId;

        std::shared_ptr<GhostSkelClip> sc, from;
        float time = 0.0f, fromTime = 0.0f, blendW = 1.0f;
        bool blending = false;
        int keyAnim = -1, keyClip = -1;
        std::string clipName;

        if (isPrev)
        {
            an.yawOffset = 0.0f; an.leanDeg = 0.0f; an.scaleMul = { 1, 1, 1 };
            sc = GetSkelClip(g_prevstate.path);
            if (!sc) continue;
            const float dur = sc->duration;
            if (dur > 0.0f)
            {
                if (g_prevstate.loop) { g_prevstate.time = fmodf(g_prevstate.time, dur); if (g_prevstate.time < 0.0f) g_prevstate.time += dur; }
                else g_prevstate.time = g_prevstate.time < 0.0f ? 0.0f : (g_prevstate.time > dur ? dur : g_prevstate.time);
            }
            else g_prevstate.time = 0.0f;
            g_prevstate.duration = dur;
            g_prevstate.matched = GhostSkin_CountMatches(sk, *sc);
            g_prevstate.total = (int)sc->tracks.size();
            time = g_prevstate.time;
            keyAnim = -100;
            keyClip = (int)(std::hash<std::string>()(g_prevstate.path) & 0x7fffffff);
            clipName = "(preview)";
        }
        else
        {
            if (an.anim.empty()) continue;
            if (!an.animResolved) { an.animResolved = true; an.animId = LoadAnimFile(an.anim); }
            if (an.animId < 0 || an.animId >= (int)g_anims.size()) continue;
            const GhostAnimAsset& a = g_anims[(size_t)an.animId];
            if (a.clips.empty()) continue;

            AnimRt& rt = g_animRt[o.id];
            animated.insert(o.id);

            if (!rt.active && !rt.entryDone)                    // the Entry node decides what plays first
            {
                rt.entryDone = true;
                const int es = GhostAnimGraph_EntryState(a.graph);
                if (es >= 0 && ModelTransition(o, a.graph.nodes[(size_t)es].name, "")) rt.blending = false;
            }

            const std::string want = rt.active ? rt.clip : an.clip;
            int ci = want.empty() ? -1 : GhostAnim_FindClip(a, want.c_str());
            if (ci < 0) ci = DefaultClipIndex(a);
            const GhostClip& c = a.clips[(size_t)ci];
            if (!c.skeletal) continue;
            sc = GetSkelClip(c.model);
            if (!sc) continue;

            float spMul = 1.0f;
            if (rt.spD > 0.0f)
            {
                if (!paused) rt.spT = fminf(rt.spT + dt, rt.spD);
                spMul = rt.spFrom + (rt.spTo - rt.spFrom) * (rt.spT / rt.spD);
            }
            if (!paused && an.playing) an.time += dt * an.speed * c.speed * spMul;
            const float dur = sc->duration;
            if (dur > 0.0f) { if (c.loop) an.time = fmodf(an.time, dur); else if (an.time > dur) an.time = dur; }
            else an.time = 0.0f;
            time = an.time;

            if (rt.blending)
            {
                if (!paused) { rt.bt += dt; rt.fromTime += dt * rt.fromSpeed; }
                from = GetSkelClip(rt.fromPath);
                if (!from || rt.bt >= rt.bd) rt.blending = false;
                else
                {
                    const float fd = from->duration;
                    if (fd > 0.0f) rt.fromTime = rt.fromLoop ? fmodf(rt.fromTime, fd) : fminf(rt.fromTime, fd);
                    blending = true; fromTime = rt.fromTime; blendW = Ease(rt.curve, rt.bt / rt.bd);
                }
            }
            UpdateAnimModifiers(o, a, rt, dt, playing);
            keyAnim = an.animId; keyClip = ci; clipName = c.name;
        }

        SkinInst& si = g_skinInst[o.id];
        if (si.slot < 0 || si.meshId != o.meshId)
        {
            if (si.slot >= 0) { GhostReleaseSkinMesh(g_pipeline, si.slot); g_skinSlotUsed[si.slot] = false; }
            int slot = -1;
            for (int s2 = 0; s2 < (int)GHOST_MAX_SKINS; s2++) if (!g_skinSlotUsed[s2]) { slot = s2; break; }
            const GhostMesh& gm = g_pipeline->meshes[o.meshId];
            if (slot < 0 || gm.cpuVerts.size() != sk.verts.size() ||
                GhostCreateSkinMesh(g_rhi, g_pipeline, slot, o.meshId, (unsigned int)sk.verts.size()) != GHOST_OK)
            {
                g_skinInst.erase(o.id);
                continue;
            }
            g_skinSlotUsed[slot] = true;
            si = SkinInst();
            si.slot = slot; si.meshId = o.meshId;
            si.verts.resize(sk.verts.size());
            memcpy(si.verts.data(), gm.cpuVerts.data(), sk.verts.size() * sizeof(GhostMeshVertex));
        }
        si.seen = true;
        if (!blending && si.lastAnim == keyAnim && si.lastClip == keyClip && fabsf(si.lastTime - time) < 1e-5f) continue;
        if (si.lastAnim != keyAnim || si.lastClip != keyClip)
        {
            char msg[256];
            snprintf(msg, sizeof msg, "[GhostX] 3D clip \"%s\": %d of %d tracks match the skeleton, %.2f s\n",
                clipName.c_str(), GhostSkin_CountMatches(sk, *sc), (int)sc->tracks.size(), sc->duration);
            OutputDebugStringA(msg);
        }
        si.lastAnim = keyAnim; si.lastClip = keyClip; si.lastTime = time;

        GhostSkin_Pose(sk, sc.get(), time, g_boneScratch);
        GhostSkin_Apply(sk, g_boneScratch, reinterpret_cast<float*>(si.verts.data()));
        if (blending)
        {
            g_blendScratch.assign(si.verts.begin(), si.verts.end());
            GhostSkin_Pose(sk, from.get(), fromTime, g_boneScratch);
            GhostSkin_Apply(sk, g_boneScratch, reinterpret_cast<float*>(g_blendScratch.data()));
            BlendVerts(si.verts.data(), g_blendScratch.data(), blendW, si.verts.size());
        }
        if (void* dst = GhostSkinMapped(g_pipeline, si.slot))
            memcpy(dst, si.verts.data(), si.verts.size() * sizeof(GhostMeshVertex));
    }

    for (auto it = g_animRt.begin(); it != g_animRt.end(); )
        if (!animated.count(it->first)) it = g_animRt.erase(it); else ++it;

    std::vector<int> dead;
    for (auto& kv : g_skinInst) if (!kv.second.seen) dead.push_back(kv.first);
    for (int id : dead) FreeSkinInstance(id);
}

static GhostObject* FindSprite(int id)
{
    GhostObject* o = FindObject(id);
    return (o && o->meshId == GHOST_MESH_QUAD) ? o : nullptr;
}

static int SetSpriteAnimImpl(GhostSprite& s, const std::string& path)
{
    s.anim = path;
    s.animTime = 0.0f;
    s.animResolved = true;
    s.animId = path.empty() ? -1 : LoadAnimFile(path);
    return (path.empty() || s.animId >= 0) ? 1 : 0;
}

// ---- 3D models driven through the Sprite* calls (the game code stays the same) ----
static GhostObject* AnimModelOf(int id)                         // an object that plays 3D clips, or null
{
    GhostObject* o = FindObject(id);
    return (o && o->meshId != GHOST_MESH_QUAD && g_skinByMesh.find(o->meshId) != g_skinByMesh.end()) ? o : nullptr;
}

static void ResolveAnimator(GhostAnimator& an)
{
    if (an.animResolved) return;
    an.animResolved = true;
    an.animId = an.anim.empty() ? -1 : LoadAnimFile(an.anim);
}

static bool HasSkeletalClips(int animId)
{
    if (animId < 0 || animId >= (int)g_anims.size()) return false;
    for (const GhostClip& c : g_anims[(size_t)animId].clips) if (c.skeletal) return true;
    return false;
}

// 1 = the clip is (now) playing, 0 = no such 3D clip. Asking for the clip that already plays does not restart it.
// Switch a model to a new state: run the wired effects and start the crossfade.
// 1 = ok, 0 = no such 3D clip. Asking for the state that already plays does nothing.
static int ModelTransition(GhostObject& o, const std::string& state, const std::string& wantClip)
{
    GhostAnimator& an = o.animator;
    ResolveAnimator(an);
    if (!HasSkeletalClips(an.animId)) return 0;
    const GhostAnimAsset& a = g_anims[(size_t)an.animId];

    auto it = g_animRt.find(o.id);
    const std::string curState = (it != g_animRt.end() && it->second.active) ? it->second.state : std::string();

    GhostGraphPath path;
    const bool inGraph = GhostAnimGraph_Resolve(a.graph, curState, state, path);
    std::string clipName = wantClip;
    if (inGraph && !a.graph.nodes[(size_t)path.target].clip.empty()) clipName = a.graph.nodes[(size_t)path.target].clip;

    int ci = clipName.empty() ? DefaultClipIndex(a) : GhostAnim_FindClip(a, clipName.c_str());
    if (ci < 0 || !a.clips[(size_t)ci].skeletal) return 0;

    AnimRt& rt = g_animRt[o.id];
    const std::string curName = rt.active ? rt.clip : an.clip;
    int curIdx = curName.empty() ? -1 : GhostAnim_FindClip(a, curName.c_str());
    if (curIdx < 0) curIdx = DefaultClipIndex(a);
    const bool sameClip = (curIdx == ci);
    if (rt.active && sameClip && _stricmp(rt.state.c_str(), state.c_str()) == 0) return 1;

    float bd = 0.12f; int curve = 3;                                    // default crossfade
    rt.spFrom = rt.spTo = 1.0f; rt.spT = rt.spD = 0.0f;
    for (int ni : path.fx)
    {
        const GhostGraphNode& n = a.graph.nodes[(size_t)ni];
        switch (n.type)
        {
        case GNODE_BLEND:  bd = n.p[0]; curve = n.curve; break;
        case GNODE_SQUASH: rt.fx.push_back({ 0.0f, fmaxf(n.p[1], 0.05f), n.p[0] * (n.p[2] > 0.5f ? -1.0f : 1.0f) }); break;
        case GNODE_SPEED:  rt.spFrom = n.p[0]; rt.spTo = n.p[1]; rt.spD = fmaxf(n.p[2], 0.01f); rt.spT = 0.0f; break;
        case GNODE_SOUND:
        {
            const int si = GhostAudioBank_Find(n.name.c_str());
            if (si >= 0) GhostAudioBank_PlayIndex(si, 0.0f, false);
            break;
        }
        case GNODE_RUMBLE: if (g_playState == GhostPlayState::Playing) GhostPad_Rumble(n.p[0], n.p[1], n.p[2]); break;
        }
    }

    if (!sameClip)
    {
        const GhostClip& cc = a.clips[(size_t)curIdx];
        rt.fromPath = cc.model; rt.fromTime = an.time; rt.fromSpeed = cc.speed * an.speed; rt.fromLoop = cc.loop;
        rt.bd = bd < 0.0f ? 0.0f : bd; rt.bt = 0.0f; rt.curve = curve; rt.blending = rt.bd > 0.001f;
        an.time = 0.0f;
    }
    rt.active = true; rt.entryDone = true;
    rt.clip = a.clips[(size_t)ci].name;
    rt.state = state;
    return 1;
}

static int Api_IsModel(int id)
{
    GhostObject* o = FindObject(id);
    const GhostMeshInfo* mi = o ? GhostMesh_Info(o->meshId) : nullptr;
    return (mi && !mi->builtin) ? 1 : 0;
}
// Seconds a model's 3D clip lasts. Accepts a state name too (the States table). 0 = unknown.
static float Api_ModelClipLength(int id, const char* clip)
{
    GhostObject* m = AnimModelOf(id);
    if (!m || !clip) return 0.0f;
    GhostAnimator& an = m->animator;
    ResolveAnimator(an);
    if (an.animId < 0 || an.animId >= (int)g_anims.size()) return 0.0f;
    const GhostAnimAsset& a = g_anims[(size_t)an.animId];
    std::string name = clip;
    for (const auto& kv : a.states)
        if (_stricmp(kv.first.c_str(), clip) == 0) { if (!kv.second.empty()) name = kv.second; break; }
    const int ci = GhostAnim_FindClip(a, name.c_str());
    if (ci < 0 || !a.clips[(size_t)ci].skeletal) return 0.0f;
    std::shared_ptr<GhostSkelClip> sc = GetSkelClip(a.clips[(size_t)ci].model);
    return sc ? sc->duration : 0.0f;
}

static int Api_SpriteSetClip(int id, const char* name)
{
    if (GhostObject* m = AnimModelOf(id)) return name ? ModelTransition(*m, name, name) : 0;
    GhostObject* o = FindSprite(id);
    if (!o || !name) return 0;
    GhostSprite& s = o->sprite;

    std::string want = name;
    if (s.animId >= 0 && s.animId < (int)g_anims.size())
    {
        const GhostAnimAsset& a = g_anims[(size_t)s.animId];
        const int ci = want.empty() ? -1 : GhostAnim_FindClip(a, name);
        if (!want.empty() && ci < 0) return 0;
        if (ci >= 0) want = a.clips[(size_t)ci].name;             // canonical spelling, so repeated calls don't restart
    }
    if (s.clip == want) return 1;
    s.clip = want;
    s.animTime = 0.0f;
    return 1;
}

static int Api_SpriteSetFlip(int id, int flip)
{
    if (AnimModelOf(id)) return 1;                               // models face by rotation, not by flipping
    GhostObject* o = FindSprite(id); if (!o) return 0; o->sprite.flipX = (flip != 0); return 1;
}
static int Api_SpriteSetFrame(int id, int f) { GhostObject* o = FindSprite(id); if (!o) return 0; o->sprite.frame = f < 0 ? 0 : (f > 4095 ? 4095 : f); return 1; }
static int Api_SpriteSetAnim(int id, const char* p)
{
    if (GhostObject* m = AnimModelOf(id))
    {
        GhostAnimator& an = m->animator;
        const std::string path = p ? p : "";
        if (path.empty()) return 0;                              // game code never clears a model's animation
        if (_stricmp(path.c_str(), an.anim.c_str()) == 0) return 1;   // already the current one
        const int aid = LoadAnimFile(path);
        if (!HasSkeletalClips(aid)) return 0;                    // a sprite-sheet file: ignore it, keep what plays now
        an.anim = path; an.animId = aid; an.animResolved = true;
        an.clip.clear(); an.time = 0.0f;
        g_animRt.erase(m->id);
        return 1;
    }
    GhostObject* o = FindSprite(id);
    return o ? SetSpriteAnimImpl(o->sprite, p ? p : "") : 0;
}
static int Api_SpriteSetSpeed(int id, float v)
{
    if (GhostObject* m = AnimModelOf(id)) { m->animator.speed = v != v ? 1.0f : (v < 0.0f ? 0.0f : (v > 10.0f ? 10.0f : v)); return 1; }
    GhostObject* o = FindSprite(id); if (!o) return 0; o->sprite.animSpeed = v < 0.0f ? 0.0f : (v > 10.0f ? 10.0f : v); return 1;
}
static int Api_SpriteSetState(int id, const char* state)
{
    if (GhostObject* m = AnimModelOf(id))
    {
        if (!state) return 0;
        GhostAnimator& an = m->animator;
        ResolveAnimator(an);
        std::string clip = state;
        if (an.animId >= 0 && an.animId < (int)g_anims.size())
            for (const auto& kv : g_anims[(size_t)an.animId].states)   // the States table: state name -> clip
                if (_stricmp(kv.first.c_str(), state) == 0) { if (!kv.second.empty()) clip = kv.second; break; }
        return ModelTransition(*m, state, clip);
    }
    GhostObject* o = FindSprite(id);
    if (!o || !state) return 0;
    std::string clip = state;
    const GhostSprite& s = o->sprite;
    if (s.animId >= 0 && s.animId < (int)g_anims.size())
        for (const auto& kv : g_anims[(size_t)s.animId].states)
            if (_stricmp(kv.first.c_str(), state) == 0) { if (!kv.second.empty()) clip = kv.second; break; }
    return Api_SpriteSetClip(id, clip.c_str());
}
static int Api_LoadScene(const char* rel)
{
    if (!rel || !*rel || g_playState == GhostPlayState::Editing) return 0;
    const std::string abs = StoredToAbs(rel);
    if (!std::filesystem::exists(std::filesystem::path(GhostUtf8ToWide(abs)))) return 0;
    g_pendingScene = rel;
    return 1;
}

static int Api_GetSceneName(char* buf, int size)
{
    if (!buf || size <= 0) return 0;
    buf[0] = 0;
    if (g_scenePath.empty()) return 0;
    const size_t s = g_scenePath.find_last_of("\\/");
    CopyMsg(s == std::string::npos ? g_scenePath : g_scenePath.substr(s + 1), buf, size);
    return 1;
}

static void Api_Quit() { g_quitRequested = true; }
static int Api_GetObjectProps(int id, GhostObjectProps* out)
{
    GhostObject* o = FindObject(id);
    if (!o || !out) return 0;
    memset(out, 0, sizeof * out);
    strncpy_s(out->link, sizeof out->link, o->link.c_str(), _TRUNCATE);
    out->pathOffset[0] = o->pathOffset.x; out->pathOffset[1] = o->pathOffset.y; out->pathOffset[2] = o->pathOffset.z;
    out->moveSpeed = o->moveSpeed;  out->movePause = o->movePause;
    out->pathMode = o->pathMode; out->timeOffset = o->timeOffset,   out->breakEffect = o->breakEffect;
    out->contents = o->contents;    out->portalTrigger = o->portalTrigger;
    out->locked = o->locked ? 1 : 0;
    out->powerType = o->powerType; out->enemyPathMode = o->enemyPathMode; out->flying = o->flying; out->enemyDeathEffect = o->enemyDeathEffect; out->itemMoves = o->itemMoves; out->coinCount = o->coinCount;
    out->player = o->playerP;
    out->enemy = o->enemyP;
    out->boss = o->bossP;
    out->path = o->pathP;
    return 1;
}

static int   Api_PadConnected() { return GhostPad_Connected() ? 1 : 0; }
static int   Api_PadButtonDown(int b) { return GhostPad_ButtonDown(b) ? 1 : 0; }
static int   Api_PadButtonPressed(int b) { return GhostPad_ButtonPressed(b) ? 1 : 0; }
static int   Api_PadButtonReleased(int b) { return GhostPad_ButtonReleased(b) ? 1 : 0; }
static float Api_PadAxis(int a) { return GhostPad_Axis(a); }
static void  Api_PadRumble(float s, float w, float sec) { GhostPad_Rumble(s, w, sec); }
static int   Api_PadActive() { return GhostPad_PadActive() ? 1 : 0; }

static int AudioPlayKind(const char* n, int kind, float fade)
{
    const int i = GhostAudioBank_Find(n);
    GhostSoundDef* d = GhostAudioBank_At(i);
    return (d && d->kind == kind && GhostAudioBank_PlayIndex(i, fade, false)) ? 1 : 0;
}
static int   Api_PlaySfx(const char* n) { return AudioPlayKind(n, GHOST_SND_SFX, 0.0f); }
static int   Api_PlayMusic(const char* n, float f) { return AudioPlayKind(n, GHOST_SND_MUSIC, f); }
static int   Api_PlayAmbience(const char* n, float f) { return AudioPlayKind(n, GHOST_SND_AMBIENCE, f); }
static void  Api_StopMusic(float f) { GhostAudio_StopMusic(f); }
static void  Api_StopAmbience(float f) { GhostAudio_StopAmbience(f); }
static void  Api_PlaySceneAudio(float f) { PlaySceneAudio(f); }
static void  Api_SetBusVolume(int b, float v) { GhostAudio_SetBusVolume(b, v); }
static float Api_GetBusVolume(int b) { return GhostAudio_GetBusVolume(b); }
static void  Api_SetBusEnabled(int b, int e) { GhostAudio_SetBusMute(b, e == 0); }
static int   Api_GetBusEnabled(int b) { return GhostAudio_GetBusMute(b) ? 0 : 1; }
static void  Api_SetSoundEnabled(const char* n, int e)
{
    GhostSoundDef* d = GhostAudioBank_At(GhostAudioBank_Find(n));
    if (!d) return;
    d->enabled = (e != 0);
    if (!d->enabled && d->kind != GHOST_SND_SFX)               // switching off the track that is playing stops it
    {
        const std::string p = GhostAudioBank_Resolve(*d);
        if (d->kind == GHOST_SND_MUSIC && p == GhostAudio_MusicPath()) GhostAudio_StopMusic(0.3f);
        if (d->kind == GHOST_SND_AMBIENCE && p == GhostAudio_AmbiencePath()) GhostAudio_StopAmbience(0.3f);
    }
}
static int   Api_GetSoundEnabled(const char* n) { GhostSoundDef* d = GhostAudioBank_At(GhostAudioBank_Find(n)); return (d && d->enabled) ? 1 : 0; }
static int Api_GetTextureId(int id)
{
    GhostObject* o = FindObject(id);
    if (!o) return 0;
    return o->meshId == GHOST_MESH_QUAD ? o->sprite.textureId : o->faces[0].textureId;
}
static int Api_SetTextureId(int id, int tex) { return GhostEngine_SetObjectTexture(id, tex); }

static int Api_SetResolution(int w, int h)
{
    const bool valid = (w == 0 && h == 0) ||
        (w >= GHOST_VIDEO_MIN_DIM && h >= GHOST_VIDEO_MIN_DIM && w <= GHOST_VIDEO_MAX_DIM && h <= GHOST_VIDEO_MAX_DIM);
    if (!valid) return 0;
    GhostVideoSettings s = g_video;
    s.resW = w; s.resH = h;
    GhostEngine_SetVideoSettings(&s);
    return 1;
}
static void Api_GetResolution(int* wh) { if (wh) { wh[0] = g_mainRW; wh[1] = g_mainRH; } }
static int  Api_GetResolutionList(int* out, int maxPairs) { return GhostEngine_GetResolutionList(out, maxPairs); }
static void Api_SetFullscreen(int mode)
{
    GhostVideoSettings s = g_video;
    s.displayMode = mode;
    GhostEngine_SetVideoSettings(&s);
}
static int  Api_GetFullscreen() { return g_video.displayMode; }
static void Api_SetScaleFilter(int f)
{
    GhostVideoSettings s = g_video;
    s.scaleFilter = f;
    GhostEngine_SetVideoSettings(&s);
}

static void BuildGameApi()
{
    GhostGameAPI& a = g_gameApi;
    memset(&a, 0, sizeof a);
    a.version = GHOST_GAME_API_VERSION;
    a.size = (int)sizeof(GhostGameAPI);
    a.Log = Api_Log;  a.GetTime = Api_GetTime;  a.GetFixedDeltaTime = Api_GetDt;  a.SetHudText = Api_SetHud;
    a.KeyDown = Api_KeyDown;  a.KeyPressed = Api_KeyPressed;  a.KeyReleased = Api_KeyReleased;
    a.FindByTag = Api_FindByTag;  a.FindAllByTag = Api_FindAllByTag;  a.GetTag = Api_GetTag;  a.SetTag = Api_SetTag;
    a.GetPosition = Api_GetPosition;  a.SetPosition = Api_SetPosition;
    a.GetRotation = Api_GetRotation;  a.SetRotation = Api_SetRotation;
    a.GetScale = Api_GetScale;  a.SetScale = Api_SetScale;
    a.GetColor = Api_GetColor;  a.SetColor = Api_SetColor;
    a.GetVisible = Api_GetVisible;  a.SetVisible = Api_SetVisible;
    a.GetBounds = Api_GetBounds;  a.QueryBox = Api_QueryBox;
    a.Spawn = Api_Spawn;  a.Destroy = Api_Destroy;
    a.GetActiveCamera = Api_GetActiveCamera;  a.SetActiveCamera = Api_SetActiveCamera;
    a.GetCamera = Api_GetCamera;  a.SetCamera = Api_SetCamera;
    a.GetMouseDelta = Api_GetMouseDelta;  a.GetMouseWheel = Api_GetMouseWheel;
    a.GetCameraMode = Api_GetCameraMode;  a.GetCameraTarget = Api_GetCameraTarget;  a.SetCameraTarget = Api_SetCameraTarget;
    a.UIRect = Api_UIRect;  a.UIImage = Api_UIImage;  a.UIText = Api_UIText;
    a.UIMeasureText = Api_UIMeasure;  a.UIGetScreenSize = Api_UIScreenSize;  a.LoadTexture = Api_LoadTexture;
    a.UILoad = Api_UILoad;  a.UIShow = Api_UIShow;  a.UISetText = Api_UISetText;  a.UISetProgress = Api_UISetProgress;
    a.UISetVisible = Api_UISetVisible;  a.UISetColor = Api_UISetColor;
    a.UIButtonClicked = Api_UIButtonClicked;  a.UIButtonDown = Api_UIButtonDown;  a.UIGetMouse = Api_UIGetMouse;
    a.SkyGetTime = Api_SkyGetTime;  a.SkySetTime = Api_SkySetTime;
    a.SpriteSetClip = Api_SpriteSetClip;  a.SpriteSetFlip = Api_SpriteSetFlip;  a.SpriteSetFrame = Api_SpriteSetFrame;
    a.SpriteSetAnim = Api_SpriteSetAnim;  a.SpriteSetSpeed = Api_SpriteSetSpeed;
    a.LoadScene = Api_LoadScene;  a.GetSceneName = Api_GetSceneName;  a.Quit = Api_Quit;
    a.GetObjectProps = Api_GetObjectProps;
    a.SpriteSetState = Api_SpriteSetState;
    a.PadConnected = Api_PadConnected;  a.PadButtonDown = Api_PadButtonDown;
    a.PadButtonPressed = Api_PadButtonPressed;  a.PadButtonReleased = Api_PadButtonReleased;
    a.PadAxis = Api_PadAxis;  a.PadRumble = Api_PadRumble;  a.PadActive = Api_PadActive;
    a.PlaySfx = Api_PlaySfx;  a.PlayMusic = Api_PlayMusic;  a.PlayAmbience = Api_PlayAmbience;
    a.StopMusic = Api_StopMusic;  a.StopAmbience = Api_StopAmbience;  a.PlaySceneAudio = Api_PlaySceneAudio;
    a.SetBusVolume = Api_SetBusVolume;  a.GetBusVolume = Api_GetBusVolume;
    a.SetBusEnabled = Api_SetBusEnabled;  a.GetBusEnabled = Api_GetBusEnabled;
    a.SetSoundEnabled = Api_SetSoundEnabled;  a.GetSoundEnabled = Api_GetSoundEnabled;
    a.GetTextureId = Api_GetTextureId;  a.SetTextureId = Api_SetTextureId;
    a.GetCollider = Api_GetCollider;
    a.SetResolution = Api_SetResolution;  a.GetResolution = Api_GetResolution;  a.GetResolutionList = Api_GetResolutionList;
    a.SetFullscreen = Api_SetFullscreen;  a.GetFullscreen = Api_GetFullscreen;  a.SetScaleFilter = Api_SetScaleFilter;
    a.IsModel = Api_IsModel;
    a.ModelClipLength = Api_ModelClipLength;
    a.IsTerrain = Api_IsTerrain;
    a.GetTerrainHeight = Api_GetTerrainHeight;
}

// Runs between steps (never inside game code). The editor's own scene stays safe in g_playBackup.
static void PerformSceneSwitch()
{
    const std::string rel = g_pendingScene;
    g_pendingScene.clear();
    if (g_playState == GhostPlayState::Editing || rel.empty()) return;
    g_animRt.clear();
    const std::string abs = StoredToAbs(rel);
    GhostGameModule_Unload();                           // Shutdown: the old level's code lets go of its object ids

    GhostScene backup = g_scene;
    std::string err, warn;
    g_modelWarn.clear();
    if (!GhostScene_Load(g_scene, g_projectRoot, abs, LoadSceneTexture, MAX_SCENE_OBJECT_COUNT, err, warn, LoadModelFile))
    {
        std::vector<GhostTextureInfo> tex = g_scene.textures;
        g_scene = backup;
        g_scene.textures = tex;
        g_playFault = "Could not load the scene \"" + rel + "\":\n" + err;
        GhostEngine_Stop();
        return;
    }
    if (!warn.empty() || !g_modelWarn.empty())
        OutputDebugStringA(("[GhostX] Scene \"" + rel + "\":\n" + warn + g_modelWarn).c_str());

    g_scenePath = abs;
    FreeAllTileInstances();
    g_scene.version++;
    if (g_scene.gameStartCameraId && FindCamera(g_scene.gameStartCameraId))
        g_scene.activeCameraId = g_scene.gameStartCameraId;
    g_hoverId = 0; g_dragging = false; g_dragId = 0; g_inputCameraId = 0;
    g_uiAssets.clear(); g_uiQuads.clear();
    g_uiMouseX = g_uiMouseY = -1.0f; g_uiMouseInside = false;
    g_skyScroll[0] = g_skyScroll[1] = 0.0f; g_cloudScroll[0] = g_cloudScroll[1] = 0.0f;
    g_mouseAccumX = g_mouseAccumY = g_apiMouseDX = g_apiMouseDY = 0.0f;
    g_wheelAccum = g_apiWheel = 0.0f;
    g_hudText.clear();
    g_stepper.Reset();
    GhostAudio_StopOneShots();
    PlaySceneAudio(1.0f);                          // the new level's music crossfades in
    BuildGameApi();
    if (GhostGameModule_Load(g_gameModulePath, &g_gameApi, g_playMessage) == GhostModuleResult::Error)
    {
        g_playFault = "The game module failed to start in the new scene:\n" + g_playMessage;
        GhostEngine_Stop();
    }
}


// ============================================================
//  Exported: play mode
// ============================================================
int GhostEngine_Play()
{

    if (!g_rhi || !g_pipeline) { g_playMessage = "The engine is not ready."; return 0; }
    if (g_playState != GhostPlayState::Editing) return 1;
    g_animRt.clear();
    g_playBackup = g_scene;
    g_playBackupPath = g_scenePath;
    g_pendingScene.clear();
    g_quitRequested = false;
    ReloadAnims();
    if (g_scene.gameStartCameraId && FindCamera(g_scene.gameStartCameraId))
        g_scene.activeCameraId = g_scene.gameStartCameraId;      // Stop restores the backup, so your editor camera comes back
    for (GhostCamera& c : g_scene.cameras) c.orbitValid = false; // orbit cameras snap to their target on the first frame
    g_mouseAccumX = g_mouseAccumY = g_apiMouseDX = g_apiMouseDY = 0.0f;
    g_wheelAccum = g_apiWheel = 0.0f;
    g_scene.version++;                                           // the hierarchy shows the new (Main View)
    g_stepper.Reset();
    g_input.Reset();
    g_playTime = 0.0; g_skyScroll[0] = g_skyScroll[1] = 0.0f; g_cloudScroll[0] = g_cloudScroll[1] = 0.0f;
    g_hudText.clear(); g_uiQuads.clear(); g_playMessage.clear(); g_playFault.clear();
    g_uiAssets.clear();
    g_uiMouseX = g_uiMouseY = -1.0f; g_uiMouseInside = false;
    g_hoverId = 0; g_dragging = false; g_dragId = 0;
    g_playState = GhostPlayState::Playing;
    for (int b = 0; b < GHOST_BUS_COUNT; b++) { g_preBusVol[b] = GhostAudio_GetBusVolume(b); g_preBusMute[b] = GhostAudio_GetBusMute(b); }
    g_haveBusSnap = true;                          // the game may change the buses: Stop puts them back
    GhostAudio_SetPaused(false);
    GhostAudio_StopAll(0.0f);                      // editor previews never leak into the game
    PlaySceneAudio(0.5f);                          // before the module loads, so its Init can override
    BuildGameApi();
    GhostModuleResult r = GhostGameModule_Load(g_gameModulePath, &g_gameApi, g_playMessage);
    OutputDebugStringA(("[GhostX] " + g_playMessage + "\n").c_str());

    if (r == GhostModuleResult::Error)
    {
        GhostEngine_Stop();                          // restores the scene: Init may already have changed it
        return 0;
    }
    return r == GhostModuleResult::Loaded ? 1 : 2;
}

void GhostEngine_Stop()
{
    if (g_playState == GhostPlayState::Editing) return;
    g_animRt.clear();
    g_scenePath = g_playBackupPath;                  // the editor goes back to the scene you were editing
    g_pendingScene.clear();
    GhostGameModule_Unload();                        // Shutdown, then free the temp copy
    GhostAudio_SetPaused(false);
    GhostAudio_StopAll(0.25f);
    if (g_haveBusSnap)
    {
        for (int b = 0; b < GHOST_BUS_COUNT; b++) { GhostAudio_SetBusVolume(b, g_preBusVol[b]); GhostAudio_SetBusMute(b, g_preBusMute[b]); }
        g_haveBusSnap = false;
    }
    GhostAudioBank_Load(g_projectRoot);            // drops runtime SetSoundEnabled changes
    g_hudText.clear(); g_uiQuads.clear();
    g_uiAssets.clear();
    g_uiMouseX = g_uiMouseY = -1.0f; g_uiMouseInside = false;
    // Textures loaded during play stay on the GPU, so keep the registry. Everything else goes back.
    std::vector<GhostTextureInfo> tex = g_scene.textures;
    unsigned int version = g_scene.version;
    g_scene = std::move(g_playBackup);
    g_scene.textures = tex;
    g_scene.version = version + 1;                   // the hierarchy rebuilds
    g_playBackup = GhostScene();
    FreeAllTileInstances();
    g_playState = GhostPlayState::Editing;
    g_hoverId = 0; g_dragging = false; g_dragId = 0;
}

void GhostEngine_SetPaused(int paused)
{
    if (paused && g_playState == GhostPlayState::Playing) { g_playState = GhostPlayState::Paused; GhostAudio_SetPaused(true); }
    else if (!paused && g_playState == GhostPlayState::Paused) { g_playState = GhostPlayState::Playing; GhostAudio_SetPaused(false); }
}

int GhostEngine_GetPlayState() { return (int)g_playState; }

// ============================================================
//  Exported: gameplay data
// ============================================================
int GhostEngine_GetGameplay(int objectId, GhostGameplayInfo* out)
{
    GhostObject* o = FindObject(objectId);
    if (!o || !out) return 0;
    *out = { o->id, o->role, o->collider ? 1 : 0,
             o->colliderSize.x, o->colliderSize.y, o->colliderSize.z,
             o->colliderOffset.x, o->colliderOffset.y, o->colliderOffset.z,
             o->visible ? 1 : 0,
             o->locked ? 1 : 0, o->pathMode, o->breakEffect, o->contents, o->portalTrigger,
                         o->pathOffset.x, o->pathOffset.y, o->pathOffset.z, o->moveSpeed, o->movePause, o->powerType, o->itemMoves, o->coinCount,
             o->enemyPathMode,o->flying , o->enemyDeathEffect, o->colliderRotation.x, o->colliderRotation.y, o->colliderRotation.z, o->timeOffset };
        
    return 1;
}

void GhostEngine_SetGameplay(const GhostGameplayInfo* in)
{
    if (!in) return;
    GhostObject* o = FindObject(in->id);
    if (!o) return;
    auto size = [](float v) { v = fabsf(v); return v < 0.01f ? 0.01f : v; };
    o->role = in->role < 0 ? 0 : (in->role >= GHOST_ROLE_COUNT ? GHOST_ROLE_COUNT - 1 : in->role);
    o->collider = (in->collider != 0);
    o->colliderSize = { size(in->csx), size(in->csy), size(in->csz) };
    o->colliderOffset = { in->cox, in->coy, in->coz };
    o->visible = (in->visible != 0);
    auto ci = [](int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); };
    auto cf = [](float v, float lo, float hi) { return v != v ? lo : (v < lo ? lo : (v > hi ? hi : v)); };
    o->locked = (in->locked != 0);
    o->pathMode = ci(in->pathMode, 0, 3);
    o->timeOffset = cf(in->timeOffset, 0.0f, 3600.0f);
    o->breakEffect = ci(in->breakEffect, 0, 2);
    o->contents = ci(in->contents, 0, 3);
    o->portalTrigger = ci(in->portalTrigger, 0, 2);
    o->pathOffset = { cf(in->moveX, -10000.0f, 10000.0f), cf(in->moveY, -10000.0f, 10000.0f), cf(in->moveZ, -10000.0f, 10000.0f) };
    o->moveSpeed = cf(in->moveSpeed, 0.01f, 100.0f);
    o->movePause = cf(in->movePause, 0.0f, 60.0f);
    o->powerType = ci(in->powerType, 0, 5);
    o->itemMoves = ci(in->itemMoves, 0, 2);
    o->coinCount = ci(in->coinCount, 1, 999);
    o->enemyPathMode = ci(in->enemyPathMode, 0, 3);
    o->flying = ci(in->flying, 0, 2);
    o->enemyDeathEffect = ci(in->enemyDeathEffect, 0, 3);
    o->colliderRotation = { cf(in->crx, -720.0f, 720.0f), cf(in->cry, -720.0f, 720.0f), cf(in->crz, -720.0f, 720.0f) };
}

int GhostEngine_GetPlayerParams(int objectId, GhostPlayerParams* out)
{
    GhostObject* o = FindObject(objectId);
    if (!o || !out) return 0;
    *out = o->playerP;
    return 1;
}

void GhostEngine_SetPlayerParams(int objectId, const GhostPlayerParams* in)
{
    GhostObject* o = FindObject(objectId);
    if (!o || !in) return;
    GhostPlayerParams p = *in;
    GhostPlayerParams_Clamp(&p);
    o->playerP = p;
}

int GhostEngine_GetEnemyParams(int objectId, GhostEnemyParams* out)
{
    GhostObject* o = FindObject(objectId);
    if (!o || !out) return 0;
    *out = o->enemyP;
    return 1;
}

void GhostEngine_SetEnemyParams(int objectId, const GhostEnemyParams* in)
{
    GhostObject* o = FindObject(objectId);
    if (!o || !in) return;
    GhostEnemyParams p = *in;
    GhostEnemyParams_Clamp(&p);
    o->enemyP = p;
}

int GhostEngine_GetBossParams(int objectId, GhostBossParams* out)
{
    GhostObject* o = FindObject(objectId);
    if (!o || !out) return 0;
    *out = o->bossP;
    return 1;
}
void GhostEngine_SetBossParams(int objectId, const GhostBossParams* in)
{
    GhostObject* o = FindObject(objectId);
    if (!o || !in) return;
    GhostBossParams p = *in;
    GhostBossParams_Clamp(&p);
    o->bossP = p;
}
int GhostEngine_GetPathParams(int objectId, GhostPathParams* out)
{
    GhostObject* o = FindObject(objectId);
    if (!o || !out) return 0;
    *out = o->pathP;
    return 1;
}
void GhostEngine_SetPathParams(int objectId, const GhostPathParams* in)
{
    GhostObject* o = FindObject(objectId);
    if (!o || !in) return;
    GhostPathParams p = *in;
    GhostPathParams_Clamp(&p);
    o->pathP = p;
}

int GhostEngine_GetCustomTag(int objectId, char* buf, int bufSize)
{
    if (!buf || bufSize <= 0) return 0;
    buf[0] = 0;
    GhostObject* o = FindObject(objectId);
    if (!o) return 0;
    CopyMsg(o->customTag, buf, bufSize);
    return 1;
}

void GhostEngine_SetCustomTag(int objectId, const char* utf8Tag)
{
    GhostObject* o = FindObject(objectId);
    if (!o) return;
    std::string t = utf8Tag ? utf8Tag : "";
    if (t.size() > 31) t.resize(31);
    o->customTag = t;
}

void GhostEngine_SetGameModulePath(const char* path) { g_gameModulePath = path ? path : ""; }

int GhostEngine_GetPlayMessage(char* buf, int size)
{
    if (!buf || size <= 0) return 0;
    CopyMsg(g_playMessage, buf, size);
    return 1;
}

int GhostEngine_GetHudText(char* buf, int size)
{
    if (!buf || size <= 0) return 0;
    CopyMsg(g_hudText, buf, size);
    return 1;
}

int GhostEngine_TakePlayFault(char* buf, int size)
{
    if (g_playFault.empty() || !buf || size <= 0) return 0;
    CopyMsg(g_playFault, buf, size);
    g_playFault.clear();
    return 1;
}
int GhostEngine_DuplicateEntity(int id)
{
    if (g_playState != GhostPlayState::Editing) return 0;
    GhostObject* src = FindObject(id);
    if (!src || !CanAddObject(src->meshId)) return 0;

    GhostObject copy = *src;                         // copy first: push_back can move the vector
    copy.id = g_scene.nextId++;

    std::string base = copy.name;                    // "Cube 4" -> "Cube", then add the new id
    size_t sp = base.find_last_of(' ');
    if (sp != std::string::npos && sp + 1 < base.size() &&
        base.find_first_not_of("0123456789", sp + 1) == std::string::npos)
        base.resize(sp);
    copy.name = base + " " + std::to_string(copy.id);
    copy.position.x += fabsf(copy.scale.x) + 0.25f;  // lands beside the original

    g_scene.objects.push_back(copy);
    g_scene.selectedId = copy.id;
    g_scene.selectedFace = -1;
    g_scene.version++;
    return copy.id;
}
int GhostEngine_DuplicateArray(int id, const float* offsets, int count, int* outIds)
{
    if (g_playState != GhostPlayState::Editing || !offsets || count <= 0) return 0;
    GhostObject* src = FindObject(id);
    if (!src) return 0;

    const GhostObject tmpl = *src;                   // copy first: push_back can move the vector

    std::string base = tmpl.name;                    // "Cube 4" -> "Cube", then add each new id
    size_t sp = base.find_last_of(' ');
    if (sp != std::string::npos && sp + 1 < base.size() &&
        base.find_first_not_of("0123456789", sp + 1) == std::string::npos)
        base.resize(sp);

    int made = 0, lastId = 0;
    for (int i = 0; i < count; i++)
    {
        if (!CanAddObject(tmpl.meshId)) break;       // scene is full: keep what was made
        GhostObject copy = tmpl;
        copy.id = g_scene.nextId++;
        copy.name = base + " " + std::to_string(copy.id);
        copy.position = { tmpl.position.x + offsets[i * 3], tmpl.position.y + offsets[i * 3 + 1], tmpl.position.z + offsets[i * 3 + 2] };
        g_scene.objects.push_back(copy);
        if (outIds) outIds[made] = copy.id;
        lastId = copy.id;
        made++;
    }
    if (made > 0)
    {
        g_scene.selectedId = lastId;
        g_scene.selectedFace = -1;
        g_scene.version++;
    }
    return made;
}

// ---- Clipboard (objects) ----
static std::vector<GhostObject> g_clip;
static int g_pasteCount = 0;

int GhostEngine_CopySelected()
{
    g_clip.clear();
    for (int id : SelectedIds()) if (GhostObject* o = FindObject(id)) g_clip.push_back(*o);
    g_pasteCount = 0;
    return g_clip.empty() ? 0 : 1;
}

int GhostEngine_Paste()
{
    if (g_playState != GhostPlayState::Editing || g_clip.empty()) return 0;
    float lo = 1e30f, hi = -1e30f;                       // one shift for the whole group keeps its layout
    for (const GhostObject& c : g_clip)
    {
        lo = fminf(lo, c.position.x - fabsf(c.scale.x) * 0.5f); hi = fmaxf(hi, c.position.x + fabsf(c.scale.x) * 0.5f);
    }
    g_pasteCount++;
    const float shift = (hi - lo + 0.25f) * (float)g_pasteCount;

    std::vector<int> made;
    for (const GhostObject& src : g_clip)
    {
        if (!CanAddObject(src.meshId)) break;
        GhostObject copy = src;
        std::string base = copy.name;
        size_t sp = base.find_last_of(' ');
        if (sp != std::string::npos && sp + 1 < base.size() && base.find_first_not_of("0123456789", sp + 1) == std::string::npos)
            base.resize(sp);
        copy.id = g_scene.nextId++;
        copy.name = base + " " + std::to_string(copy.id);
        copy.position.x += shift;
        g_scene.objects.push_back(copy);
        made.push_back(copy.id);
    }
    if (made.empty()) return 0;
    GhostEngine_SetSelection(made.data(), (int)made.size());
    g_scene.version++;
    return 1;
}

// ---- Jump the editor camera to the selection ----
int GhostEngine_FocusSelected()
{
    if (g_playState != GhostPlayState::Editing) return 0;
    GhostCamera* cam = InputCamera();
    GhostVec3* pp = EntityPosition(g_scene.selectedId);
    if (!cam || !pp) return 0;
    GhostVec3 tgt;
    if (cam->mode == 1 && ResolveOrbitTarget(*cam, tgt)) return 0;     // an orbit camera follows its target

    GhostVec3 center = *pp;
    float radius = 1.0f;
    if (const GhostObject* o = FindObject(g_scene.selectedId))
    {
        const GhostVec3 mn = GhostObject_Min(*o), mx = GhostObject_Max(*o);
        center = GhostVec3(o->position.x + (mn.x + mx.x) * 0.5f * o->scale.x,
            o->position.y + (mn.y + mx.y) * 0.5f * o->scale.y,
            o->position.z + (mn.z + mx.z) * 0.5f * o->scale.z);
        const float ex = fabsf((mx.x - mn.x) * o->scale.x), ey = fabsf((mx.y - mn.y) * o->scale.y), ez = fabsf((mx.z - mn.z) * o->scale.z);
        radius = 0.5f * sqrtf(ex * ex + ey * ey + ez * ez);
    }
    const float dist = fmaxf(radius * 2.5f, 2.0f);
    cam->position = center - AnglesToDir(cam->yawDeg, cam->pitchDeg) * dist;
    return 1;
}

void GhostEngine_SetEntityName(int id, const char* utf8)
{
    if (!utf8) return;
    std::string n = utf8;
    while (!n.empty() && n.back() == ' ') n.pop_back();
    if (n.empty()) return;
    if (n.size() > 63) n.resize(63);
    for (GhostObject& o : g_scene.objects) if (o.id == id) { o.name = n; g_scene.version++; return; }
    for (GhostLight& l : g_scene.lights) if (l.id == id) { l.name = n; g_scene.version++; return; }
    for (GhostCamera& c : g_scene.cameras) if (c.id == id) { c.name = n; g_scene.version++; return; }
    for (GhostModEntity& m : g_scene.mods) if (m.id == id) { m.name = n; g_scene.version++; return; }
}

void GhostEngine_SetFaceMode(int on) { g_faceMode = (on != 0); }
int  GhostEngine_GetFaceMode() { return g_faceMode ? 1 : 0; }

int GhostEngine_GetCameraTargetTag(int id, char* buf, int size)
{
    if (!buf || size <= 0) return 0;
    buf[0] = 0;
    GhostCamera* c = FindCamera(id);
    if (!c) return 0;
    CopyMsg(c->targetTag, buf, size);
    return 1;
}

void GhostEngine_SetCameraTargetTag(int id, const char* tag)
{
    GhostCamera* c = FindCamera(id);
    if (!c) return;
    std::string t = tag ? tag : "";
    if (t.size() > 31) t.resize(31);
    c->targetTag = t;
    c->orbitValid = false;
}

int GhostEngine_GetGameStartCamera() { return g_scene.gameStartCameraId; }

void GhostEngine_SetGameStartCamera(int id)
{
    g_scene.gameStartCameraId = (id != 0 && FindCamera(id)) ? id : 0;
}

void GhostEngine_MouseWheel(int delta)
{
    if (!g_inputEnabled) return;
    const float notches = (float)delta / 120.0f;

    if (g_playState == GhostPlayState::Editing)       // wheel = dolly, right mouse + wheel = fly speed
    {
        GhostCamera* c = InputCamera();
        if (!c) return;
        GhostVec3 tgt;
        if (c->mode == 1 && ResolveOrbitTarget(*c, tgt))
        {
            const float d = c->distance - notches;
            c->distance = d < c->minDist ? c->minDist : (d > c->maxDist ? c->maxDist : d);
        }
        else if (GetAsyncKeyState(VK_RBUTTON) & 0x8000)
        {
            const float v = c->moveSpeed * powf(1.25f, notches);
            c->moveSpeed = v < 0.1f ? 0.1f : (v > 200.0f ? 200.0f : v);
        }
        else
        {
            const float step = fmaxf(c->moveSpeed * 0.3f, 0.25f) * ((GetAsyncKeyState(VK_SHIFT) & 0x8000) ? 4.0f : 1.0f);
            c->position = c->position + AnglesToDir(c->yawDeg, c->pitchDeg) * (notches * step);
        }
        return;
    }

    if (g_playState != GhostPlayState::Playing) return;
    g_wheelAccum += notches;                       // the game can read it too

    GhostCamera* c = InputCamera();
    if (c && c->mode == 1)
    {
        float d = c->distance - notches;
        c->distance = d < c->minDist ? c->minDist : (d > c->maxDist ? c->maxDist : d);
    }
}

int GhostEngine_SetFont(int tex, int aw, int ah, float cw, float ch, float pad, float lh, const float* adv)
{
    if (!adv || tex < 0 || tex >= (int)g_scene.textures.size() || aw <= 0 || ah <= 0 ||
        cw <= 0.0f || ch <= 0.0f || lh <= 0.0f) return 0;

    GhostFont f;
    f.textureId = tex;
    f.atlasW = aw; f.atlasH = ah;
    f.cellW = cw;  f.cellH = ch;
    f.pad = pad;   f.lineHeight = lh;
    memcpy(f.advance, adv, 95 * sizeof(float));
    f.advance[95] = 0.0f;
    f.ready = true;
    g_font = f;
    return 1;
}

int GhostEngine_RegisterFont(int tex, int aw, int ah, float cw, float ch, float pad, float lh, const float* adv)
{
    if (!adv || tex < 0 || tex >= (int)g_scene.textures.size() || aw <= 0 || ah <= 0 ||
        cw <= 0.0f || ch <= 0.0f || lh <= 0.0f || g_fonts.size() >= 32) return 0;
    GhostFont f;
    f.textureId = tex; f.atlasW = aw; f.atlasH = ah;
    f.cellW = cw; f.cellH = ch; f.pad = pad; f.lineHeight = lh;
    memcpy(f.advance, adv, 95 * sizeof(float));
    f.advance[95] = 0.0f;
    f.ready = true;
    g_fonts.push_back(f);
    return (int)g_fonts.size();
}

void GhostEngine_SetFontProvider(GhostFontProviderFn fn) { g_fontProvider = fn; }

// ============================================================
//  Exported: mods
// ============================================================
static GhostModEntity* FindMod(int id)
{
    for (GhostModEntity& m : g_scene.mods) if (m.id == id) return &m;
    return nullptr;
}

int GhostEngine_GetModTypeCount() { return GHOST_MOD_COUNT; }

int GhostEngine_GetModTypeName(int t, char* buf, int size)
{
    const GhostModType* ty = GhostMods_Type(t);
    if (!ty || !buf || size <= 0) return 0;
    CopyMsg(ty->name, buf, size);
    return 1;
}

int GhostEngine_GetModParamCount(int t)
{
    const GhostModType* ty = GhostMods_Type(t);
    return ty ? ty->paramCount : 0;
}

int GhostEngine_GetModParamInfo(int t, int i, GhostModParamInfo* out)
{
    const GhostModType* ty = GhostMods_Type(t);
    if (!ty || !out || i < 0 || i >= ty->paramCount) return 0;
    const GhostParamDesc& d = ty->params[i];
    memset(out, 0, sizeof * out);
    out->kind = d.kind;
    out->mn = d.mn; out->mx = d.mx;
    for (int k = 0; k < 3; k++) out->def[k] = d.def[k];
    out->count = GhostParam_Count(d);
    strncpy_s(out->name, sizeof out->name, d.name, _TRUNCATE);
    strncpy_s(out->category, sizeof out->category, d.category, _TRUNCATE);
    if (d.enums) strncpy_s(out->enums, sizeof out->enums, d.enums, _TRUNCATE);
    return 1;
}

int GhostEngine_AddMod(int typeId, int sx, int sy)
{
    if (g_playState != GhostPlayState::Editing) return 0;
    const GhostModType* ty = GhostMods_Type(typeId);
    if (!ty) return 0;

    int have = 0;
    GhostModEntity* first = nullptr;
    for (GhostModEntity& m : g_scene.mods) if (m.typeId == typeId) { if (!first) first = &m; have++; }
    if (first && ty->maxPerScene > 0 && have >= ty->maxPerScene)
    {
        g_scene.selectedId = first->id;              // already there: select it instead
        return 0;
    }

    SpawnCoords(sx, sy);
    GhostModEntity* m = GhostScene_AddMod(g_scene, typeId);
    if (!m) return 0;
    GhostVec3 p = SpawnPoint(sx, sy);
    p.y += 1.5f;
    m->position = p;
    g_scene.selectedId = m->id;
    return m->id;
}

int GhostEngine_GetMod(int id, float* pos)
{
    GhostModEntity* m = FindMod(id);
    if (!m) return -1;
    if (pos) { pos[0] = m->position.x; pos[1] = m->position.y; pos[2] = m->position.z; }
    return m->typeId;
}

void GhostEngine_SetModPosition(int id, const float* pos)
{
    GhostModEntity* m = FindMod(id);
    if (m && pos) m->position = { pos[0], pos[1], pos[2] };
}

int GhostEngine_GetModParam(int id, int index, float* out3)
{
    GhostModEntity* m = FindMod(id);
    const GhostModType* ty = m ? GhostMods_Type(m->typeId) : nullptr;
    if (!ty || !out3 || index < 0 || index >= ty->paramCount) return 0;
    const int off = GhostMods_ParamOffset(m->typeId, index), n = GhostParam_Count(ty->params[index]);
    if (off < 0 || (size_t)(off + n) > m->values.size()) return 0;
    for (int i = 0; i < 3; i++) out3[i] = i < n ? m->values[(size_t)(off + i)] : 0.0f;
    return 1;
}

void GhostEngine_SetModParam(int id, int index, const float* in3)
{
    GhostModEntity* m = FindMod(id);
    const GhostModType* ty = m ? GhostMods_Type(m->typeId) : nullptr;
    if (!ty || !in3 || index < 0 || index >= ty->paramCount) return;
    const GhostParamDesc& d = ty->params[index];
    const int off = GhostMods_ParamOffset(m->typeId, index), n = GhostParam_Count(d);
    if (off < 0 || (size_t)(off + n) > m->values.size()) return;

    float v[3] = { in3[0], in3[1], in3[2] };
    GhostMods_Clamp(m->typeId, index, v);
    if (d.kind == GHOST_PARAM_TEXTURE && (int)v[0] >= (int)g_scene.textures.size()) v[0] = 0.0f;
    for (int i = 0; i < n; i++) m->values[(size_t)(off + i)] = v[i];
}


// ============================================================
//  Exported: sprites
// ============================================================
int GhostEngine_GetSprite(int id, GhostSpriteInfo* out)
{
    GhostObject* o = FindSprite(id);
    if (!o || !out) return 0;
    const GhostSprite& s = o->sprite;
    int tex, cols, rows, frame;
    SpriteDrawInfo(*o, tex, cols, rows, frame);
    *out = { id, s.textureId, s.source, s.cols, s.rows, s.frame,
             s.flipX, s.billboard, s.unlit, s.alphaClip, s.pixelArt, s.playing,
             s.tint[0], s.tint[1], s.tint[2], s.animSpeed,
             tex, cols, rows, frame };
    return 1;
}

void GhostEngine_SetSprite(const GhostSpriteInfo* in)
{
    GhostObject* o = in ? FindSprite(in->id) : nullptr;
    if (!o) return;
    auto ci = [](int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); };
    auto cf = [](float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); };
    GhostSprite& s = o->sprite;
    s.textureId = (in->textureId >= 0 && in->textureId < (int)g_scene.textures.size()) ? in->textureId : 0;
    s.source = in->source ? 1 : 0;
    s.cols = ci(in->cols, 1, 64);
    s.rows = ci(in->rows, 1, 64);
    s.frame = ci(in->frame, 0, 4095);
    s.flipX = in->flipX != 0;
    s.billboard = in->billboard != 0;
    s.unlit = in->unlit != 0;
    s.alphaClip = in->alphaClip != 0;
    s.pixelArt = in->pixelArt != 0;
    s.playing = in->playing != 0;
    s.tint[0] = cf(in->tintR, 0.0f, 1.0f); s.tint[1] = cf(in->tintG, 0.0f, 1.0f); s.tint[2] = cf(in->tintB, 0.0f, 1.0f);
    s.animSpeed = cf(in->animSpeed, 0.0f, 10.0f);
}

int GhostEngine_GetSpriteText(int id, int which, char* buf, int size)
{
    if (!buf || size <= 0) return 0;
    buf[0] = 0;
    GhostObject* o = FindSprite(id);
    if (!o) return 0;
    std::string t;
    if (which == 0) t = o->sprite.anim;
    else if (which == 1) t = o->sprite.clip;
    else if (o->sprite.animId >= 0 && o->sprite.animId < (int)g_anims.size())
        for (const GhostClip& c : g_anims[(size_t)o->sprite.animId].clips) { if (!t.empty()) t += "|"; t += c.name; }
    CopyMsg(t, buf, size);
    return 1;
}

void GhostEngine_SetSpriteText(int id, int which, const char* text)
{
    GhostObject* o = FindSprite(id);
    if (!o) return;
    if (which == 0) SetSpriteAnimImpl(o->sprite, text ? text : "");
    else if (which == 1) Api_SpriteSetClip(id, text ? text : "");
}

void GhostEngine_ReloadAnimations() { ReloadAnims(); }

// ============================================================
//  Exported: shadows
// ============================================================
int GhostEngine_GetObjectShadow(int id, int* mode, int* lightId, int* receive)
{
    GhostObject* o = FindObject(id);
    if (!o) return 0;
    if (mode) *mode = o->shadowMode;
    if (lightId) *lightId = o->shadowLightId;
    if (receive) *receive = o->receiveShadows ? 1 : 0;
    return 1;
}

void GhostEngine_SetObjectShadow(int id, int mode, int lightId, int receive)
{
    GhostObject* o = FindObject(id);
    if (!o) return;
    if (mode < 0) mode = 0;
    if (mode > 2) mode = 2;
    if (mode == 2)
    {
        bool ok = false;
        for (const GhostLight& l : g_scene.lights) if (l.id == lightId) ok = true;
        if (!ok) { mode = 1; lightId = 0; }
    }
    o->shadowMode = mode;
    o->shadowLightId = (mode == 2) ? lightId : 0;
    o->receiveShadows = (receive != 0);
}

int GhostEngine_GetLightShadows(int id)
{
    for (const GhostLight& l : g_scene.lights) if (l.id == id) return l.castShadows ? 1 : 0;
    return -1;
}

void GhostEngine_SetLightShadows(int id, int on)
{
    for (GhostLight& l : g_scene.lights) if (l.id == id) { l.castShadows = (on != 0); return; }
}

void GhostEngine_GetShadowSettings(int* enabled, float* strength)
{
    if (enabled) *enabled = g_scene.shadowsEnabled ? 1 : 0;
    if (strength) *strength = g_scene.shadowStrength;
}

void GhostEngine_SetShadowSettings(int enabled, float strength)
{
    g_scene.shadowsEnabled = (enabled != 0);
    g_scene.shadowStrength = strength < 0.0f ? 0.0f : (strength > 1.0f ? 1.0f : strength);
}

int GhostEngine_GetShadowResolution() { return g_scene.shadowRes; }

void GhostEngine_SetShadowResolution(int size) { g_scene.shadowRes = GhostShadowRes_Clamp(size); }

void GhostEngine_GetShadowTuning(GhostShadowTuning* out)
{
    if (!out) return;
    *out = { g_scene.shadowFilter, g_scene.shadowSoftness, g_scene.shadowBias, g_scene.shadowNormalBias, g_scene.shadowDistance };
}

void GhostEngine_SetShadowTuning(const GhostShadowTuning* in)
{
    if (!in) return;
    auto cf = [](float v, float lo, float hi) { return v != v ? lo : (v < lo ? lo : (v > hi ? hi : v)); };
    g_scene.shadowFilter = in->filter < 0 ? 0 : (in->filter > 3 ? 3 : in->filter);
    g_scene.shadowSoftness = cf(in->softness, 0.25f, 4.0f);
    g_scene.shadowBias = cf(in->bias, 0.0f, 8.0f);
    g_scene.shadowNormalBias = cf(in->normalBias, 0.0f, 8.0f);
    const float d = cf(in->distance, 0.0f, 1000.0f);
    g_scene.shadowDistance = (d > 0.0f && d < 2.0f) ? 2.0f : d;        // 0 = whole scene, otherwise at least 2
}


int GhostEngine_GetTileMap(int id, GhostTileMapInfo* out)
{
    GhostObject* o = FindObject(id);
    if (!o || !out || !o->tileMap.enabled) return 0;
    const GhostTileMap& t = o->tileMap;
    *out = { o->id, t.tileSize, t.gridX, t.gridY, t.gridZ, t.direction, o->receiveShadows ? 1 : 0, t.shadowLightId, t.layerCount, t.subdiv, t.autoCollider ? 1 : 0 };
    return 1;
}

void GhostEngine_SetTileMap(const GhostTileMapInfo* in)
{
    if (!in) return;
    GhostObject* o = FindObject(in->id);
    if (!o || !o->tileMap.enabled) return;
    GhostTileMap& t = o->tileMap;
    const int oldGX = t.gridX, oldGY = t.gridY, oldSW = t.splatW, oldSH = t.splatH;
    const int oldSub = GhostTileMap_EffSub(t.gridX, t.gridY, t.subdiv);
    t.tileSize = in->tileSize;
    t.gridX = in->gridX; t.gridY = in->gridY; t.gridZ = in->gridZ;
    t.direction = in->direction;
    t.subdiv = in->subdiv;
    t.autoCollider = (in->autoCollider != 0);
    t.shadowLightId = 0;                                                    // only a real directional light is accepted
    if (in->shadowLightId != 0)
        for (const GhostLight& l : g_scene.lights)
            if (l.id == in->shadowLightId && l.type == GhostLightType::Directional) t.shadowLightId = l.id;
    o->receiveShadows = (in->receiveShadows != 0);
    GhostTileMap_Clamp(t);
    if (t.gridX != oldGX || t.gridY != oldGY || GhostTileMap_EffSub(t.gridX, t.gridY, t.subdiv) != oldSub)
        GhostTileMap_Reshape(t, oldGX, oldGY, oldSub, oldSW, oldSH);        // heights and paint stay where they were
    GhostTileMap_EnsureTiles(t);
}

int GhostEngine_AddTileLayer(int id)
{
    if (g_playState != GhostPlayState::Editing) return -1;
    GhostObject* o = FindObject(id);
    if (!o || !o->tileMap.enabled) return -1;
    GhostTileMap& t = o->tileMap;
    if (t.layerCount >= GHOST_TILEMAP_MAX_LAYERS) return -1;
    GhostTileMap_EnsureTiles(t);
    const int layer = t.layerCount;
    o->faces[layer] = GhostMaterial();                                      // a fresh material. Layer 0 covers everything until you paint
    t.layerCount++;
    t.sStamp = GhostTile_NextStamp();
    return layer;
}

int GhostEngine_RemoveTileLayer(int id, int layer)
{
    if (g_playState != GhostPlayState::Editing) return 0;
    GhostObject* o = FindObject(id);
    if (!o || !o->tileMap.enabled) return 0;
    GhostTileMap& t = o->tileMap;
    if (layer < 0 || layer >= t.layerCount) return 0;
    GhostTileMap_EnsureTiles(t);
    for (int l = layer; l < t.layerCount - 1; l++) o->faces[l] = o->faces[l + 1];
    o->faces[t.layerCount - 1] = GhostMaterial();

    const size_t texels = (size_t)t.splatW * (size_t)t.splatH;              // its weight goes to layer 0 (or layer 1 when layer 0 is removed)
    for (size_t i = 0; i < texels; i++)
    {
        unsigned char* b = &t.splat[i * 8];
        const int dst = (layer == 0) ? 1 : 0;
        const int moved = (int)b[dst] + (int)b[layer];
        b[dst] = (unsigned char)(moved > 255 ? 255 : moved);
        for (int l = layer; l < 7; l++) b[l] = b[l + 1];
        b[7] = 0;
    }
    t.layerCount--;
    t.sStamp = GhostTile_NextStamp();
    if (g_scene.selectedId == id && g_scene.selectedFace >= t.layerCount)
        g_scene.selectedFace = t.layerCount > 0 ? t.layerCount - 1 : -1;
    return 1;
}

// Fills a block of tiles (columns x0..x1, rows y0..y1) with one layer. Returns the number of tiles covered.
int GhostEngine_AssignTileLayer(int id, int layer, int x0, int y0, int x1, int y1)
{
    if (g_playState != GhostPlayState::Editing) return 0;
    GhostObject* o = FindObject(id);
    if (!o || !o->tileMap.enabled) return 0;
    GhostTileMap& t = o->tileMap;
    if (layer < 0 || layer >= t.layerCount) return 0;
    GhostTileMap_EnsureTiles(t);
    if (x0 > x1) std::swap(x0, x1);
    if (y0 > y1) std::swap(y0, y1);
    x0 = x0 < 0 ? 0 : x0;  y0 = y0 < 0 ? 0 : y0;
    x1 = x1 >= t.gridX ? t.gridX - 1 : x1;  y1 = y1 >= t.gridY ? t.gridY - 1 : y1;
    if (x0 > x1 || y0 > y1) return 0;

    const int c0 = x0 * t.splatW / t.gridX, c1 = (x1 + 1) * t.splatW / t.gridX - 1;
    const int r0 = y0 * t.splatH / t.gridY, r1 = (y1 + 1) * t.splatH / t.gridY - 1;
    for (int r = r0; r <= r1; r++)
        for (int c = c0; c <= c1; c++)
        {
            unsigned char* b = &t.splat[((size_t)r * t.splatW + c) * 8];
            memset(b, 0, 8);
            b[layer] = 255;
        }
    t.sStamp = GhostTile_NextStamp();
    return (x1 - x0 + 1) * (y1 - y0 + 1);
}

// outCounts[l] = how much of the map layer l dominates, in tiles (approximate once painted)
int GhostEngine_GetTileLayerCounts(int id, int* out, int maxCount)
{
    GhostObject* o = FindObject(id);
    if (!o || !o->tileMap.enabled) return 0;
    const GhostTileMap& t = o->tileMap;
    const int n = maxCount < GHOST_TILEMAP_MAX_LAYERS ? maxCount : GHOST_TILEMAP_MAX_LAYERS;
    if (out && n > 0)
    {
        for (int l = 0; l < n; l++) out[l] = 0;
        const size_t texels = (size_t)t.splatW * (size_t)t.splatH;
        if (t.layerCount > 0 && texels > 0 && t.splat.size() == texels * 8)
        {
            int cnt[GHOST_TILEMAP_MAX_LAYERS] = {};
            for (size_t i = 0; i < texels; i++)
            {
                const unsigned char* b = &t.splat[i * 8];
                int best = 0;
                for (int l = 1; l < t.layerCount; l++) if (b[l] > b[best]) best = l;
                cnt[best]++;
            }
            const double tiles = (double)t.gridX * (double)t.gridY;
            for (int l = 0; l < n; l++) out[l] = (int)((double)cnt[l] * tiles / (double)texels + 0.5);
        }
    }
    return t.layerCount;
}

void GhostEngine_ResetTerrain(int id, int what)
{
    if (g_playState != GhostPlayState::Editing) return;
    GhostObject* o = FindObject(id);
    if (!o || !o->tileMap.enabled) return;
    GhostTileMap& t = o->tileMap;
    GhostTileMap_EnsureTiles(t);
    if (what & 1)
    {
        std::fill(t.height.begin(), t.height.end(), 0.0f);
        GhostTileMap_UpdateRange(t);
        t.hStamp = GhostTile_NextStamp();
    }
    if (what & 2)
    {
        const size_t texels = (size_t)t.splatW * (size_t)t.splatH;
        for (size_t i = 0; i < texels; i++) { memset(&t.splat[i * 8], 0, 8); t.splat[i * 8] = 255; }
        t.sStamp = GhostTile_NextStamp();
    }
}

// ---- Paint mode ----
void GhostEngine_SetPaintMode(int on)
{
    g_paintMode = (on != 0) && g_playState == GhostPlayState::Editing;
    if (!g_paintMode) g_painting = false;
}
int GhostEngine_GetPaintMode() { return g_paintMode ? 1 : 0; }

void GhostEngine_GetPaintSettings(GhostPaintInfo* out)
{
    if (!out) return;
    int layers = 0, has = 0;
    if (GhostObject* o = FindObject(g_scene.selectedId))
        if (o->tileMap.enabled) { has = 1; layers = o->tileMap.layerCount; }
    int f = CurrentFace();
    if (f < 0) f = 0;
    *out = { g_paintMode ? 1 : 0, g_brush.tool, g_brush.radius, g_brush.strength, g_brush.fade, g_brush.setHeight, f, layers, has };
}

void GhostEngine_SetPaintSettings(const GhostPaintInfo* in)
{
    if (!in) return;
    auto cf = [](float v, float lo, float hi) { return v != v ? lo : (v < lo ? lo : (v > hi ? hi : v)); };
    g_brush.tool = in->tool < 0 ? 0 : (in->tool > 6 ? 6 : in->tool);
    g_brush.radius = cf(in->radius, 0.05f, 200.0f);
    g_brush.strength = cf(in->strength, 0.01f, 1.0f);
    g_brush.fade = cf(in->fade, 0.0f, 1.0f);
    g_brush.setHeight = cf(in->setHeight, -1000.0f, 1000.0f);
    if (in->layer >= 0)                                          // the brush layer = the Active Layer, but only for a Tile Map with layers
        if (GhostObject* o = FindObject(g_scene.selectedId))
            if (o->tileMap.enabled && o->tileMap.layerCount > 0 && in->layer < o->tileMap.layerCount && in->layer != g_scene.selectedFace)
                GhostEngine_SetTargetFace(in->layer);
}


// ============================================================
//  Exported: model import, materials, re-import and .ghostmesh loading
// ============================================================
static std::string StemOf(const std::string& path)
{
    return GhostWideToUtf8(std::filesystem::path(GhostUtf8ToWide(path)).stem().wstring());
}

static std::string FileNameOf(const std::string& path)
{
    return GhostWideToUtf8(std::filesystem::path(GhostUtf8ToWide(path)).filename().wstring());
}

static std::string ModelFilePath(const std::string& name)
{
    return GhostPath_Normalize(g_projectRoot + "\\Content\\Models\\" + name + ".ghostmesh");
}

static std::string SkinPathFor(const std::string& meshFile)          // Foo.ghostmesh -> Foo.ghostskin
{
    std::filesystem::path p(GhostUtf8ToWide(meshFile));
    p.replace_extension(L".ghostskin");
    return GhostWideToUtf8(p.wstring());
}

// Loads the .ghostskin that sits next to a .ghostmesh, if there is one
static void LoadSkinSibling(int meshId, const std::string& meshFile, std::string& warn)
{
    const std::string sp = SkinPathFor(meshFile);
    if (!std::filesystem::exists(std::filesystem::path(GhostUtf8ToWide(sp)))) return;
    auto sk = std::make_shared<GhostSkinData>();
    std::string e;
    if (!GhostSkinFile_Load(sp, *sk, e)) { warn += "Skeleton: " + e + "\n"; return; }
    const GhostMeshInfo* mi = GhostMesh_Info(meshId);
    if (!mi || sk->verts.size() != mi->vertexCount) { warn += "The skeleton file doesn't match the mesh. Re-import the model.\n"; return; }
    g_skinByMesh[meshId] = sk;
}


// A name nothing else is using: not in the registry, and (when saving to the project) no existing file
static std::string UniqueModelName(std::string base, bool checkFile)
{
    for (char& c : base) if (strchr("<>:\"/\\|?*", c)) c = '_';
    if (base.empty()) base = "Model";
    std::string n = base;
    for (int k = 2; ; k++)
    {
        bool taken = GhostMesh_FindByName(n) >= 0;
        if (!taken && checkFile && !g_projectRoot.empty())
            taken = std::filesystem::exists(std::filesystem::path(GhostUtf8ToWide(ModelFilePath(n))));
        if (!taken) return n;
        n = base + " (" + std::to_string(k) + ")";
    }
}

// Project-relative with '/', or absolute when the file is outside the project
static std::string StoredRel(const std::string& absUtf8)
{
    namespace fs = std::filesystem;
    fs::path p = fs::path(GhostUtf8ToWide(absUtf8)).lexically_normal();
    if (!g_projectRoot.empty())
    {
        fs::path rel = p.lexically_relative(fs::path(GhostUtf8ToWide(g_projectRoot)).lexically_normal());
        if (!rel.empty() && *rel.begin() != fs::path(L"..")) return GhostWideToUtf8(rel.generic_wstring());
    }
    return GhostWideToUtf8(p.generic_wstring());
}

static std::string ModelTexDir(const std::string& name)
{
    if (!g_projectRoot.empty()) return GhostPath_Normalize(g_projectRoot + "\\Content\\Textures\\" + name);
    wchar_t t[MAX_PATH + 2];
    const DWORD n = GetTempPathW(MAX_PATH, t);                     // no project: textures go to a temp folder for this session
    return GhostPath_Normalize(GhostWideToUtf8(std::wstring(t, n)) + "GhostX\\ModelTextures\\" + name);
}

// Copies (or extracts) every texture the importer found into the project, and points the slot at the copy
static void PlaceModelTextures(GhostModelResult& r, const std::string& name, std::string& warn)
{
    namespace fs = std::filesystem;
    const fs::path dir(GhostUtf8ToWide(ModelTexDir(name)));
    std::map<std::string, fs::path> placed;                        // source file -> where it went
    std::set<std::wstring> usedNames;
    int embedded = 0;

    for (GhostModelSub& s : r.subs)
    {
        GhostModelMat& m = s.mat;
        if (m.texSource.empty() && m.texData.empty()) continue;

        std::error_code ec;
        fs::create_directories(dir, ec);
        fs::path dest;
        bool ok = false;

        if (!m.texSource.empty())
        {
            auto it = placed.find(m.texSource);
            if (it != placed.end()) { dest = it->second; ok = true; }
            else
            {
                const fs::path src(GhostUtf8ToWide(m.texSource));
                const std::wstring stem = src.stem().wstring(), ext = src.extension().wstring();
                dest = dir / src.filename();
                for (int k = 2; usedNames.count(dest.filename().wstring()); k++)
                    dest = dir / (stem + L"_" + std::to_wstring(k) + ext);
                usedNames.insert(dest.filename().wstring());

                if (src.lexically_normal() == dest.lexically_normal()) ok = true;
                else { fs::copy_file(src, dest, fs::copy_options::overwrite_existing, ec); ok = !ec; }
                if (ok) placed[m.texSource] = dest;
            }
        }
        else
        {
            dest = dir / (GhostUtf8ToWide(name) + L"_embedded_" + std::to_wstring(++embedded) + L"." + GhostUtf8ToWide(m.texExt));
            std::ofstream f(dest, std::ios::binary);
            f.write((const char*)m.texData.data(), (std::streamsize)m.texData.size());
            ok = (bool)f;
        }

        if (ok)
        {
            m.texPath = StoredRel(GhostWideToUtf8(dest.wstring()));
            m.tint[0] = m.tint[1] = m.tint[2] = 1.0f;              // a textured slot shows the image at full strength
        }
        else warn += "Could not copy the texture for \"" + s.name + "\" into the project.\n";
        m.texSource.clear();
        m.texData.clear();
        m.texData.shrink_to_fit();
    }
}

// One material slot of the registry: the stored defaults, with the texture loaded onto the GPU
static GhostFaceDef MakeFace(const GhostModelSub& s, std::string& warn)
{
    GhostFaceDef fd;
    fd.name = s.name;
    fd.firstIndex = s.firstIndex;
    fd.indexCount = s.indexCount;
    const GhostModelMat& m = s.mat;
    if (!m.hasMat) return fd;

    GhostMaterial g;
    for (int k = 0; k < 3; k++) { g.tint[k] = m.tint[k]; g.emissive[k] = m.emissive[k]; }
    g.metallic = m.metallic;
    g.roughness = m.roughness;
    g.specular = m.specular;
    g.emissiveStrength = m.emissiveStrength;
    {
        std::string n = s.name;
        for (char& c : n) c = (char)tolower((unsigned char)c);
        if (n.find("lash") != std::string::npos || n.find("brow") != std::string::npos)
        {
            g.alphaMode = 1; g.alphaSource = 2;
        }      // cutout, light background = clear
    }
    if (!m.texPath.empty())
    {
        const std::string abs = StoredToAbs(m.texPath);
        const int id = LoadSceneTexture(abs, FileNameOf(abs));
        if (id > 0) g.textureId = id;
        else warn += "Missing texture: " + m.texPath + "\n";
    }
    fd.mat = g;
    fd.hasMat = true;
    return fd;
}

static GhostMeshInfo BuildMeshInfo(const GhostModelResult& r, const std::string& name, const std::string& source,
    const std::string& file, std::string& warn)
{
    GhostMeshInfo mi;
    mi.name = name;
    mi.source = source;
    mi.file = file;
    mi.builtin = false;
    mi.index32 = true;
    mi.bmin = { r.bmin[0], r.bmin[1], r.bmin[2] };
    mi.bmax = { r.bmax[0], r.bmax[1], r.bmax[2] };
    mi.defaultScale = { 1, 1, 1 };
    mi.vertexCount = (unsigned int)r.vertices.size();
    mi.triangleCount = r.triangleCount;
    mi.importScale = r.opts.scale;
    mi.importFit = r.opts.fitSize;
    mi.importUp = r.opts.upAxis;
    mi.importFlags = (r.opts.flipWinding ? 1 : 0) | (r.opts.importMaterials ? 0 : 2) | (r.opts.keepSkin ? 4 : 0);
    for (const GhostModelSub& s : r.subs) mi.faces.push_back(MakeFace(s, warn));
    return mi;
}

// Uploads a converted model and adds it to the registry
static int RegisterModel(const GhostModelResult& r, const std::string& name, const std::string& source,
    const std::string& file, std::string& err, std::string& warn)
{
    static_assert(GHOST_MODEL_MAX_SLOTS == GHOST_MAX_FACES, "importer slot limit must match GHOST_MAX_FACES");
    static_assert(sizeof(GhostMeshVertex) == sizeof(GhostVertex), "GhostMeshVertex must match GhostVertex");

    if (GhostMesh_Count() >= GHOST_MAX_MESHES)
    {
        err = "The mesh limit (64) has been reached. Restart the editor to clear models you no longer use."; return -1;
    }

    const int id = GhostMesh_Count();
    if (GhostUploadMeshData(g_rhi, g_pipeline, id, r.vertices.data(), (unsigned int)r.vertices.size(),
        r.indices.data(), (unsigned int)r.indices.size()) != GHOST_OK)
    {
        err = "The GPU upload failed (out of memory?)."; return -1;
    }

    if (GhostMesh_Register(BuildMeshInfo(r, name, source, file, warn)) != id)
    {
        err = "The mesh registry is out of sync."; return -1;
    }
    if (r.skin) g_skinByMesh[id] = r.skin;
    return id;
}

// Finds a loaded mesh by its .ghostmesh path, or loads the file
static int LoadModelFile(const std::string& absFile, std::string& err)
{
    const std::string abs = GhostPath_Normalize(absFile);
    for (int i = GHOST_MESH_COUNT; i < GhostMesh_Count(); i++)
    {
        const GhostMeshInfo* m = GhostMesh_Info(i);
        if (m && !m->file.empty() && _stricmp(m->file.c_str(), abs.c_str()) == 0) return i;
    }
    if (!g_rhi || !g_pipeline) { err = "The engine is not ready."; return -1; }

    GhostModelResult r;
    std::string source;
    if (!GhostMeshFile_Load(abs, r, source, err)) return -1;

    const std::string name = UniqueModelName(StemOf(abs), false);
    r.name = name;
    std::string warn;
    const int id = RegisterModel(r, name, source, abs, err, warn);
    if (id >= 0) LoadSkinSibling(id, abs, warn);
    if (id >= 0 && !warn.empty()) g_modelWarn += "Model \"" + name + "\":\n" + warn;
    return id;
}

int GhostEngine_ImportModel(const char* path, float scale, float fitSize, int upAxis, int flags,
    char* msg, int msgSize)
{
    if (!g_rhi || !g_pipeline || !path) { CopyMsg("The engine is not ready.", msg, msgSize); return -1; }
    if (g_playState != GhostPlayState::Editing) { CopyMsg("Stop playing before importing a model.", msg, msgSize); return -1; }
    if (GhostMesh_Count() >= GHOST_MAX_MESHES) { CopyMsg("The mesh limit (64) has been reached.", msg, msgSize); return -1; }

    GhostModelOptions opt;
    opt.scale = (scale > 0.0001f && scale <= 10000.0f) ? scale : 1.0f;
    opt.fitSize = fitSize > 0.0f ? (fitSize > 10000.0f ? 10000.0f : fitSize) : 0.0f;
    opt.upAxis = upAxis < 0 ? 0 : (upAxis > 2 ? 2 : upAxis);
    opt.flipWinding = (flags & 1) != 0;
    opt.importMaterials = (flags & 2) == 0;
    opt.keepSkin = (flags & 4) != 0;
    GhostModelResult r;
    std::string err;
    if (!GhostModel_Load(path, opt, r, err)) { CopyMsg(err, msg, msgSize); return -1; }

    const std::string src = GhostPath_Normalize(path);
    const bool toProject = !g_projectRoot.empty();
    const std::string name = UniqueModelName(r.name, toProject);
    r.name = name;

    PlaceModelTextures(r, name, r.warn);

    std::string file, saveWarn;
    if (toProject)                                       // convert once; scenes load the .ghostmesh from now on
    {
        const std::string target = ModelFilePath(name);
        std::string serr;
        if (GhostMeshFile_Save(target, r, src, serr))
        {
            file = target;
            if (r.skin)
            {
                std::string kerr;
                if (!GhostSkinFile_Save(SkinPathFor(target), *r.skin, kerr))
                    saveWarn += "Could not save the skeleton: " + kerr + "\nThe animations work in this session only.\n";
            }
        }
        else saveWarn += "Could not save the converted mesh: " + serr + "\nThe model works in this session only.";
    }

    std::string regWarn;
    const int id = RegisterModel(r, name, src, file, err, regWarn);
    if (id < 0)
    {
        if (!file.empty())
        {
            std::error_code ec;
            std::filesystem::remove(std::filesystem::path(GhostUtf8ToWide(file)), ec);
        }
        CopyMsg(err, msg, msgSize);
        return -1;
    }

    std::string text = "Imported \"" + name + "\":\n" + r.info;
    if (!file.empty()) text += "\nSaved as Content/Models/" + name + ".ghostmesh";
    const std::string allWarn = r.warn + regWarn;
    if (!allWarn.empty()) text += "\n\n" + allWarn;
    if (!saveWarn.empty()) text += "\n\n" + saveWarn;
    if (!toProject) text += "\n\nNo project is open, so this model lives only in this session. Open or create a project to keep it.";
    CopyMsg(text, msg, msgSize);
    return id;
}

// ---- re-import: keep a placed object's own edits, refresh everything it hadn't touched ----
static bool NearV(const GhostVec3& a, const GhostVec3& b)
{
    return fabsf(a.x - b.x) < 0.001f && fabsf(a.y - b.y) < 0.001f && fabsf(a.z - b.z) < 0.001f;
}

static void RemapObjectToMesh(GhostObject& o, const GhostMeshInfo& oldM, const GhostMeshInfo& newM)
{
    GhostMaterial prev[GHOST_MAX_FACES];
    for (int f = 0; f < GHOST_MAX_FACES; f++) prev[f] = o.faces[f];
    const int oc = (int)oldM.faces.size() > GHOST_MAX_FACES ? GHOST_MAX_FACES : (int)oldM.faces.size();
    const int nc = (int)newM.faces.size() > GHOST_MAX_FACES ? GHOST_MAX_FACES : (int)newM.faces.size();

    for (int f = 0; f < GHOST_MAX_FACES; f++) o.faces[f] = GhostMaterial();
    for (int f = 0; f < nc; f++)
    {
        const GhostFaceDef& nf = newM.faces[(size_t)f];
        o.faces[f] = nf.hasMat ? nf.mat : GhostMaterial();
        for (int k = 0; k < oc; k++)
        {
            const GhostFaceDef& of = oldM.faces[(size_t)k];
            if (_stricmp(of.name.c_str(), nf.name.c_str()) != 0) continue;
            const GhostMaterial oldDefault = of.hasMat ? of.mat : GhostMaterial();
            if (!GhostMaterial_Equal(prev[k], oldDefault)) o.faces[f] = prev[k];   // the user changed this slot: keep it
            break;
        }
    }

    GhostVec3 os, oo, ns, no;
    GhostMesh_FitCollider(oldM, os, oo);
    GhostMesh_FitCollider(newM, ns, no);
    if (NearV(o.colliderSize, os) && NearV(o.colliderOffset, oo)) { o.colliderSize = ns; o.colliderOffset = no; }
}

int GhostEngine_ReimportModel(int meshId, int useStored, float scale, float fitSize, int upAxis, int flags,
    char* msg, int msgSize)
{
    if (!g_rhi || !g_pipeline) { CopyMsg("The engine is not ready.", msg, msgSize); return -1; }
    if (g_playState != GhostPlayState::Editing) { CopyMsg("Stop playing before re-importing a model.", msg, msgSize); return -1; }
    const GhostMeshInfo* probe = GhostMesh_Info(meshId);
    if (!probe || probe->builtin) { CopyMsg("Only imported models can be re-imported.", msg, msgSize); return -1; }

    GhostMeshInfo& cur = GhostMeshes()[(size_t)meshId];
    if (cur.source.empty() || !std::filesystem::exists(std::filesystem::path(GhostUtf8ToWide(cur.source))))
    {
        CopyMsg("The original file was not found:\n" + (cur.source.empty() ? std::string("(unknown)") : cur.source), msg, msgSize);
        return -1;
    }

    GhostModelOptions opt;
    if (useStored)
    {
        opt.scale = cur.importScale > 0.0001f ? cur.importScale : 1.0f;
        opt.fitSize = cur.importFit > 0.0f ? cur.importFit : 0.0f;
        opt.upAxis = cur.importUp;
        opt.flipWinding = (cur.importFlags & 1) != 0;
        opt.importMaterials = (cur.importFlags & 2) == 0;
        opt.keepSkin = (cur.importFlags & 4) != 0;
    }
    else
    {
        opt.scale = (scale > 0.0001f && scale <= 10000.0f) ? scale : 1.0f;
        opt.fitSize = fitSize > 0.0f ? (fitSize > 10000.0f ? 10000.0f : fitSize) : 0.0f;
        opt.upAxis = upAxis < 0 ? 0 : (upAxis > 2 ? 2 : upAxis);
        opt.flipWinding = (flags & 1) != 0;
        opt.importMaterials = (flags & 2) == 0;
        opt.keepSkin = (flags & 4) != 0;
    }

    GhostModelResult r;
    std::string err;
    if (!GhostModel_Load(cur.source, opt, r, err)) { CopyMsg(err, msg, msgSize); return -1; }   // nothing has changed yet
    const std::string name = cur.name;
    r.name = name;
    PlaceModelTextures(r, name, r.warn);

    if (GhostUploadMeshData(g_rhi, g_pipeline, meshId, r.vertices.data(), (unsigned int)r.vertices.size(),
        r.indices.data(), (unsigned int)r.indices.size()) != GHOST_OK)
    {
        CopyMsg("The GPU upload failed (out of memory?).", msg, msgSize); return -1;
    }

    std::string file = cur.file, saveWarn;
    if (file.empty() && !g_projectRoot.empty()) file = ModelFilePath(name);
    if (!file.empty())
    {
        std::string serr;
        if (!GhostMeshFile_Save(file, r, cur.source, serr))
        {
            saveWarn = "Could not save the converted mesh: " + serr; file = cur.file;
        }
    }

    const GhostMeshInfo old = cur;
    std::string regWarn;
    cur = BuildMeshInfo(r, name, old.source, file, regWarn);
    ReleaseSkinInstancesOfMesh(meshId);
    if (r.skin)
    {
        g_skinByMesh[meshId] = r.skin;
        if (!file.empty()) { std::string kerr; GhostSkinFile_Save(SkinPathFor(file), *r.skin, kerr); }
    }
    else g_skinByMesh.erase(meshId);
    for (GhostObject& o : g_scene.objects)
        if (o.meshId == meshId && o.missingMesh.empty() && o.missingMeshFile.empty()) RemapObjectToMesh(o, old, cur);
    g_scene.selectedFace = -1;
    g_scene.version++;                                           // the hierarchy and Details refresh

    std::string text = "Re-imported \"" + name + "\":\n" + r.info;
    const std::string allWarn = r.warn + regWarn;
    if (!allWarn.empty()) text += "\n\n" + allWarn;
    if (!saveWarn.empty()) text += "\n\n" + saveWarn;
    CopyMsg(text, msg, msgSize);
    return meshId;
}

void GhostEngine_ApplyMeshDefaults(int objectId, int what)
{
    if (g_playState != GhostPlayState::Editing) return;
    GhostObject* o = FindObject(objectId);
    const GhostMeshInfo* mi = o ? GhostMesh_Info(o->meshId) : nullptr;
    if (!o || !mi || !o->missingMesh.empty() || !o->missingMeshFile.empty()) return;

    if (what & 1)                                                // materials
    {
        for (int f = 0; f < GHOST_MAX_FACES; f++) o->faces[f] = GhostMaterial();
        const int n = GhostMesh_FaceCount(o->meshId);
        for (int f = 0; f < n; f++) if (mi->faces[(size_t)f].hasMat) o->faces[f] = mi->faces[(size_t)f].mat;
        o->useVertexColor = false;
    }
    if (what & 2) GhostMesh_FitCollider(*mi, o->colliderSize, o->colliderOffset);
}

int GhostEngine_LoadModelFile(const char* path, char* msg, int msgSize)
{
    if (!path) { CopyMsg("No file given.", msg, msgSize); return -1; }
    g_modelWarn.clear();
    std::string err;
    const int id = LoadModelFile(path, err);
    CopyMsg(id < 0 ? err : g_modelWarn, msg, msgSize);
    return id;
}

int GhostEngine_GetMeshCount() { return GhostMesh_Count(); }

int GhostEngine_GetMeshInfo(int id, GhostMeshStats* out)
{
    const GhostMeshInfo* m = GhostMesh_Info(id);
    if (!m || !out) return 0;
    *out = { m->builtin ? 1 : 0, GhostMesh_FaceCount(id), m->vertexCount, m->triangleCount,
             m->bmin.x, m->bmin.y, m->bmin.z, m->bmax.x, m->bmax.y, m->bmax.z,
             m->importScale, m->importFit, m->importUp, m->importFlags };
    return 1;
}

int GhostEngine_GetMeshText(int id, int which, char* buf, int size)
{
    if (!buf || size <= 0) return 0;
    buf[0] = 0;
    const GhostMeshInfo* m = GhostMesh_Info(id);
    if (!m) return 0;
    CopyMsg(which == 0 ? m->name : (which == 1 ? m->file : m->source), buf, size);
    return 1;
}

int GhostEngine_GetObjectMissingMesh(int objectId, char* buf, int size)
{
    if (buf && size > 0) buf[0] = 0;
    GhostObject* o = FindObject(objectId);
    if (!o || (o->missingMesh.empty() && o->missingMeshFile.empty())) return 0;
    if (buf && size > 0) CopyMsg(o->missingMesh.empty() ? o->missingMeshFile : o->missingMesh, buf, size);
    return 1;
}


int GhostEngine_TakeQuitRequest()
{
    const bool q = g_quitRequested;
    g_quitRequested = false;
    return q ? 1 : 0;
}

void GhostEngine_GetViewSettings(GhostViewSettings* out) { if (out) *out = g_viewCfg; }

void GhostEngine_SetViewSettings(const GhostViewSettings* in)
{
    if (!in) return;
    auto b = [](int v) { return v ? 1 : 0; };
    auto ci = [](int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); };

    GhostViewSettings s = *in;
    s.showSelection = b(s.showSelection); s.showFps = b(s.showFps);
    s.showGrid = b(s.showGrid);           s.showWorldAxes = b(s.showWorldAxes);
    s.showDims = b(s.showDims);           s.edgeLabels = b(s.edgeLabels);
    s.dimAxes = ci(s.dimAxes, 0, 7);
    s.gridPlane = ci(s.gridPlane, 0, 2);  s.gridStyle = ci(s.gridStyle, 0, 1);
    s.gridSize = (s.gridSize != s.gridSize) ? 1.0f : (s.gridSize < 0.01f ? 0.01f : (s.gridSize > 100.0f ? 100.0f : s.gridSize));
    s.gridExtent = ci(s.gridExtent, 4, 100);
    s.tool = ci(s.tool, 0, 4);            s.space = ci(s.space, 0, 1);
    s.unit = ci(s.unit, 0, 4);            s.decimals = ci(s.decimals, 0, 3);
    g_viewCfg = s;
}



void GhostEngine_GetGizmoSettings(GhostGizmoSettings* out) { if (out) *out = g_gizSet; }

void GhostEngine_SetGizmoSettings(const GhostGizmoSettings* in)
{
    if (!in) return;
    auto cf = [](float v, float lo, float hi) { return v != v ? lo : (v < lo ? lo : (v > hi ? hi : v)); };
    g_gizSet.snap = in->snap ? 1 : 0;
    g_gizSet.moveStep = cf(in->moveStep, 0.0f, 1000.0f);
    g_gizSet.rotStep = cf(in->rotStep, 0.0f, 180.0f);
    g_gizSet.scaleStep = cf(in->scaleStep, 0.0f, 10.0f);
}


int GhostEngine_TakeDimClick(int* kind, int* index, int* objectId, float* worldLen)
{
    if (!g_dimClick.valid) return 0;
    if (kind) *kind = g_dimClick.kind;
    if (index) *index = g_dimClick.index;
    if (objectId) *objectId = g_dimClick.objectId;
    if (worldLen) *worldLen = g_dimClick.len;
    g_dimClick.valid = false;
    return 1;
}

int GhostEngine_GetObjectSize(int id, float* xyz)
{
    GhostObject* o = FindObject(id);
    if (!o || !xyz) return 0;
    const GhostVec3 mn = GhostObject_Min(*o), mx = GhostObject_Max(*o);
    xyz[0] = fabsf((mx.x - mn.x) * o->scale.x);
    xyz[1] = fabsf((mx.y - mn.y) * o->scale.y);
    xyz[2] = fabsf((mx.z - mn.z) * o->scale.z);
    return 1;
}

void GhostEngine_SetObjectSize(int id, int axis, float world)
{
    GhostObject* o = FindObject(id);
    if (!o || g_playState != GhostPlayState::Editing || axis < 0 || axis > 2 || world != world) return;
    const GhostVec3 mn = GhostObject_Min(*o), mx = GhostObject_Max(*o);
    const float meshExt[3] = { fabsf(mx.x - mn.x), fabsf(mx.y - mn.y), fabsf(mx.z - mn.z) };
    if (meshExt[axis] < 1e-6f) return;

    world = world < 0.0001f ? 0.0001f : (world > 100000.0f ? 100000.0f : world);
    float* s = (axis == 0) ? &o->scale.x : (axis == 1 ? &o->scale.y : &o->scale.z);
    *s = (*s < 0.0f ? -1.0f : 1.0f) * (world / meshExt[axis]);        // the mirror sign stays
}

void GhostEngine_ScaleObject(int id, float f)
{
    GhostObject* o = FindObject(id);
    if (!o || g_playState != GhostPlayState::Editing || f != f) return;
    f = f < 0.001f ? 0.001f : (f > 1000.0f ? 1000.0f : f);
    o->scale = o->scale * f;
}

int GhostEngine_GetLinkId(int id, char* buf, int size)
{
    if (!buf || size <= 0) return 0;
    buf[0] = 0;
    GhostObject* o = FindObject(id);
    if (!o) return 0;
    CopyMsg(o->link, buf, size);
    return 1;
}

void GhostEngine_SetLinkId(int id, const char* t)
{
    GhostObject* o = FindObject(id);
    if (!o) return;
    std::string s = t ? t : "";
    if (s.size() > 63) s.resize(63);
    o->link = s;
}


int GhostEngine_GetSelectedCount() { PruneMulti(); return (g_scene.selectedId ? 1 : 0) + (int)g_multi.size(); }

int GhostEngine_GetSelectedIds(int* out, int maxCount)
{
    if (!out || maxCount <= 0) return 0;
    int n = 0;
    for (int id : SelectedIds()) { if (n >= maxCount) break; out[n++] = id; }
    return n;
}

void GhostEngine_SetSelection(const int* ids, int count)
{
    g_multi.clear(); g_scene.selectedId = 0; g_scene.selectedFace = -1;
    for (int i = 0; ids && i < count; i++)
        if (EntityPosition(ids[i])) { if (g_scene.selectedId) g_multi.push_back(g_scene.selectedId); g_scene.selectedId = ids[i]; }
}

void GhostEngine_SetColliderMode(int on) { g_colMode = (on != 0); }
int  GhostEngine_GetColliderMode() { return g_colMode ? 1 : 0; }
// ============================================================
//  Exported: antialiasing
// ============================================================
void GhostEngine_SetMSAA(int samples) { g_msaaWanted = samples < 1 ? 1 : (samples > 8 ? 8 : samples); }
int  GhostEngine_GetMSAA() { return (int)g_msaa.active; }
int  GhostEngine_GetMSAAMax() { return (int)g_msaa.maxSamples; }

void  GhostEngine_SetTAA(int on) { g_taaOn = on ? 1 : 0; }
int   GhostEngine_GetTAA() { return g_taaOn; }
void  GhostEngine_SetTAAJitterSpeed(float s) { g_taaSpeed = (s != s) ? 1.0f : (s < 0.05f ? 0.05f : (s > 4.0f ? 4.0f : s)); }
float GhostEngine_GetTAAJitterSpeed() { return g_taaSpeed; }


void GhostEngine_SetPadSettings(float deadzone, int rumble, int slot) { GhostPad_SetSettings(deadzone, rumble != 0, slot); }

int GhostEngine_GetPadInfo(GhostPadInfo* out)
{
    if (!out) return 0;
    memset(out, 0, sizeof * out);
    out->slot = -1;
    int slot = -1, buttons = 0;
    if (!GhostPad_Probe(slot, buttons, out->axes, &out->raw)) return 0;
    out->connected = 1; out->slot = slot; out->buttons = buttons;
    return 1;
}

void GhostEngine_PadTestRumble(int on) { GhostPad_TestRumble(on != 0); }

// ============================================================
//  Exported: audio
// ============================================================
int GhostEngine_GetSoundCount()
{
    if (GhostAudioBank_Count() == 0) GhostAudioBank_Load(g_projectRoot);     // the mixer can ask before Init
    return GhostAudioBank_Count();
}

int GhostEngine_GetSoundInfo(int i, GhostSoundInfo* out)
{
    GhostSoundDef* d = GhostAudioBank_At(i);
    if (!d || !out) return 0;
    memset(out, 0, sizeof * out);
    out->bus = d->bus; out->kind = d->kind; out->enabled = d->enabled ? 1 : 0;
    out->volume = d->volume; out->pitchVar = d->pitchVar;
    strncpy_s(out->name, sizeof out->name, d->name.c_str(), _TRUNCATE);
    strncpy_s(out->file, sizeof out->file, d->file.c_str(), _TRUNCATE);
    return 1;
}

void GhostEngine_SetSoundInfo(int i, const GhostSoundInfo* in)
{
    GhostSoundDef* d = GhostAudioBank_At(i);
    if (!d || !in) return;
    auto cf = [](float v, float lo, float hi) { return v != v ? lo : (v < lo ? lo : (v > hi ? hi : v)); };
    d->enabled = (in->enabled != 0);
    d->bus = in->bus < 1 ? 1 : (in->bus > 3 ? 3 : in->bus);
    d->volume = cf(in->volume, 0.0f, 2.0f);
    d->pitchVar = cf(in->pitchVar, 0.0f, 0.5f);
    char f[261]; memcpy(f, in->file, 260); f[260] = 0;
    d->file = f;
    GhostAudio_ClearMissing();
}

int  GhostEngine_SaveSoundBank() { std::string err; return GhostAudioBank_Save(err) ? 1 : 0; }
int  GhostEngine_TestSound(int i) { return GhostAudioBank_PlayIndex(i, 0.3f, true) ? 1 : 0; }
void GhostEngine_StopAudio() { GhostAudio_StopAll(0.2f); }

void  GhostEngine_SetBusVolume(int b, float v) { GhostAudio_SetBusVolume(b, v); }
float GhostEngine_GetBusVolume(int b) { return GhostAudio_GetBusVolume(b); }
void  GhostEngine_SetBusMute(int b, int m) { GhostAudio_SetBusMute(b, m != 0); }
int   GhostEngine_GetBusMute(int b) { return GhostAudio_GetBusMute(b) ? 1 : 0; }

int GhostEngine_PreviewFile(const char* path)
{
    if (!path || g_playState != GhostPlayState::Editing) return 0;
    return GhostAudio_PlayMusic(path, 1.0f, 0.1f, false) ? 1 : 0;      // streamed, so long tracks start at once
}

int GhostEngine_GetSceneAudio(int which, char* buf, int size)
{
    if (!buf || size <= 0) return 0;
    CopyMsg(which == 0 ? g_scene.music : g_scene.ambience, buf, size);
    return 1;
}

void GhostEngine_SetSceneAudio(int which, const char* utf8)
{
    std::string s = utf8 ? utf8 : "";
    if (s.size() > 47) s.resize(47);
    (which == 0 ? g_scene.music : g_scene.ambience) = s;               // the scene hash notices, so the title gets its *
}


// ============================================================
//  Exported: animated models
// ============================================================
int GhostEngine_IsMeshSkinned(int meshId) { return g_skinByMesh.find(meshId) != g_skinByMesh.end() ? 1 : 0; }

static GhostObject* FindAnimatedModel(int id)
{
    GhostObject* o = FindObject(id);
    return (o && g_skinByMesh.find(o->meshId) != g_skinByMesh.end()) ? o : nullptr;
}

int GhostEngine_GetAnimatorText(int id, int which, char* buf, int size)
{
    if (!buf || size <= 0) return 0;
    buf[0] = 0;
    GhostObject* o = FindAnimatedModel(id);
    if (!o) return 0;
    GhostAnimator& an = o->animator;
    std::string t;
    if (which == 0) t = an.anim;
    else if (which == 1) t = an.clip;
    else
    {
        if (!an.animResolved) { an.animResolved = true; an.animId = an.anim.empty() ? -1 : LoadAnimFile(an.anim); }
        if (an.animId >= 0 && an.animId < (int)g_anims.size())
            for (const GhostClip& c : g_anims[(size_t)an.animId].clips) { if (!t.empty()) t += "|"; t += c.name; }
    }
    CopyMsg(t, buf, size);
    return 1;
}

void GhostEngine_SetAnimatorText(int id, int which, const char* text)
{
    GhostObject* o = FindAnimatedModel(id);
    g_animRt.erase(id);
    if (!o) return;
    GhostAnimator& an = o->animator;
    const std::string t = text ? text : "";
    if (which == 0)
    {
        an.anim = t;
        an.clip.clear();                      // back to the default clip (Idle)
        an.time = 0.0f;
        an.animResolved = true;
        an.animId = t.empty() ? -1 : LoadAnimFile(t);
    }
    else if (which == 1) { an.clip = t; an.time = 0.0f; }
}

int GhostEngine_GetAnimator(int id, int* playing, float* speed, float* time)
{
    GhostObject* o = FindAnimatedModel(id);
    if (!o) return 0;
    if (playing) *playing = o->animator.playing ? 1 : 0;
    if (speed) *speed = o->animator.speed;
    if (time) *time = o->animator.time;
    return 1;
}

void GhostEngine_SetAnimator(int id, int playing, float speed)
{
    GhostObject* o = FindAnimatedModel(id);
    if (!o) return;
    o->animator.playing = (playing != 0);
    o->animator.speed = speed != speed ? 1.0f : (speed < 0.0f ? 0.0f : (speed > 10.0f ? 10.0f : speed));
}

// ============================================================
//  Exported: animation preview window + blueprint testing
// ============================================================
int GhostEngine_CreatePreviewViewport(HWND hwnd, unsigned int width, unsigned int height)
{
    if (!g_rhi || !g_pipeline) return 0;
    if (width == 0) width = 1;
    if (height == 0) height = 1;
    const int id = g_rhi->CreateSurface(hwnd, width, height);
    if (id <= 0) return 0;
    if (GhostResizeDepthBuffer(g_rhi, g_pipeline, id, width, height) != GHOST_OK) { g_rhi->DestroySurface(id); return 0; }
    g_vp[id] = EngineViewport();
    g_vp[id].alive = g_vp[id].visible = g_vp[id].preview = true;
    return id;
}

void GhostEngine_PreviewSetTarget(int vp, int objectId)
{
    if (vp > 0 && vp < GHOST_MAX_SURFACES && g_vp[vp].preview) g_vp[vp].solo = objectId;
}

void GhostEngine_PreviewOrbit(int vp, float dYaw, float dPitch, float zoomMul)
{
    if (vp <= 0 || vp >= GHOST_MAX_SURFACES || !g_vp[vp].preview) return;
    EngineViewport& v = g_vp[vp];
    v.pYaw += dYaw;
    v.pPitch = fmaxf(-80.0f, fminf(80.0f, v.pPitch + dPitch));
    v.pZoom = fmaxf(0.2f, fminf(5.0f, v.pZoom * (zoomMul > 0.0f ? zoomMul : 1.0f)));
}

void GhostEngine_PreviewResetCamera(int vp)
{
    if (vp <= 0 || vp >= GHOST_MAX_SURFACES || !g_vp[vp].preview) return;
    g_vp[vp].pYaw = 20.0f; g_vp[vp].pPitch = -8.0f; g_vp[vp].pZoom = 1.0f;
}

// Plays one FBX clip on a model, without touching the scene (not saved, no undo, no "*" in the title)
void GhostEngine_AnimPreview_Set(int objectId, const char* storedFbxPath, int loop, float speed, int playing)
{
    const std::string p = storedFbxPath ? storedFbxPath : "";
    const bool changed = !g_prevstate.active || g_prevstate.objectId != objectId || g_prevstate.path != p;
    g_prevstate.active = (objectId != 0 && !p.empty() && AnimModelOf(objectId) != nullptr);
    g_prevstate.objectId = objectId; g_prevstate.path = p;
    g_prevstate.loop = (loop != 0);
    g_prevstate.speed = speed != speed ? 1.0f : (speed < 0.0f ? 0.0f : (speed > 8.0f ? 8.0f : speed));
    g_prevstate.playing = (playing != 0);
    if (changed) g_prevstate.time = 0.0f;
}
void GhostEngine_AnimPreview_Stop() { g_prevstate.active = false; }
void GhostEngine_AnimPreview_Seek(float seconds) { g_prevstate.time = seconds < 0.0f ? 0.0f : seconds; }
int  GhostEngine_AnimPreview_Get(float* time, float* duration, int* matched, int* total)
{
    if (!g_prevstate.active) return 0;
    if (time) *time = g_prevstate.time;
    if (duration) *duration = g_prevstate.duration;
    if (matched) *matched = g_prevstate.matched;
    if (total) *total = g_prevstate.total;
    return 1;
}

// Blueprint test: the same call the game makes (SpriteSetState), usable in the editor
int  GhostEngine_AnimRequestState(int objectId, const char* state)
{
    if (!state || !AnimModelOf(objectId)) return 0;
    return Api_SpriteSetState(objectId, state);
}
void GhostEngine_AnimResetRuntime(int objectId) { g_animRt.erase(objectId); }