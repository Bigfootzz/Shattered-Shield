// glTF 2.0 model loader for Shattered Shield (Nov 11, 2026).
//
// Adds glTF (.gltf / .glb) as a first-class model type alongside IQM,
// using cgltf (single-header, MIT, vendored in src/engine/cgltf/).
// Blender exports glTF natively, so this removes the IQM conversion
// step from the model pipeline.
//
// Data model: like IQM, a glTF model is a skelmodel. The skeleton
// (joint hierarchy) is shared by name when models use the same rig,
// but each model's inverse bind pose is stored PER MODEL on its
// meshgroup (from the glTF skin's inverseBindMatrices), matching the
// glTF data model. The engine's skinning must use the per-model
// invbase (see skelmodel.h) or models with different bind poses
// will distort when they share a skeleton (the .81 explosion).

#define CGLTF_IMPLEMENTATION
#include "cgltf/cgltf.h"

struct gltf;

struct gltf : skelmodel, skelloader<gltf>
{
    gltf(const char *name) : skelmodel(name) {}

    static const char *formatname() { return "gltf"; }
    int type() const { return MDL_GLTF; }

    struct gltfmeshgroup : skelmeshgroup
    {
        // Per-model inverse bind pose, one dualquat per joint, from the
        // glTF skin's inverseBindMatrices. This is the model's own bind
        // pose (its proportions), NOT the shared skeleton's. Skinning
        // must use these, not skel->bones[i].invbase, when a model
        // shares a skeleton with a different bind pose.
        dualquat *invbind;
        int numinvbind;

        gltfmeshgroup() : invbind(NULL), numinvbind(0) {}
        virtual ~gltfmeshgroup() { DELETEA(invbind); }

        // Read a glTF accessor's float data into out (count*components floats).
        static bool readfloats(cgltf_accessor *acc, float *out, int count, int components)
        {
            if(!acc) return false;
            if(cgltf_accessor_unpack_floats(acc, out, count*components) == 0) return false;
            return true;
        }

        // Read a glTF accessor's indices into out (count indices).
        static bool readindices(cgltf_accessor *acc, uint *out, int count)
        {
            if(!acc) return false;
            if(cgltf_accessor_unpack_indices(acc, out, sizeof(uint), count) == 0) return false;
            return true;
        }

        // glTF is authored in metres; the engine works in units where a
        // humanoid is ~64 tall. .88 tried scaling every position/translation
        // read from glTF by 35.46 in the loader -- the model came out
        // gigantic AND deformed, so the loader scale conflicts with the
        // engine's own model->scale handling of the bone palette. Do NOT
        // scale in the loader. The size is set by mdlscale in the model's
        // cfg instead (3546 = 35.46x), which is the engine's designed
        // whole-model scaler and scales bones + vertices together.
        static constexpr float GLTF_SCALE = 1.0f;

        bool loadgltfmesh(cgltf_data *data, const char *filename)
        {
            if(data->meshes_count <= 0) return false;

            // Build the skeleton from the first skin (if any). The skin's
            // joints define the hierarchy; inverseBindMatrices give the
            // per-model bind pose.
            cgltf_skin *skin = data->skins_count > 0 ? &data->skins[0] : NULL;
            if(skin && skin->joints_count > 0)
            {
                if(skel->numbones <= 0)
                {
                    skel->numbones = (int)skin->joints_count;
                    skel->bones = new boneinfo[skel->numbones];
                    // Map glTF node -> joint index for parent lookup.
                    loopi(skel->numbones)
                    {
                        cgltf_node *node = skin->joints[i];
                        boneinfo &b = skel->bones[i];
                        if(!b.name) b.name = newstring(node->name ? node->name : "");
                        b.parent = -1;
                        if(node->parent)
                        {
                            loopj(skel->numbones) if(skin->joints[j] == node->parent) { b.parent = j; break; }
                        }
                        if(skel->shared <= 1)
                        {
                            // glTF node local transform -> bind pose. Use the
                            // node's TRS (same source as the animation code,
                            // so bind pose and animation agree). Convert
                            // Y-up (glTF) to the engine's space: negate Y
                            // in the translation, negate x and z in the quat
                            // (the IQM-proven conversion).
                            vec pos(0, 0, 0);
                            quat q(0, 0, 0, 1);
                            if(node->has_translation)
                                pos = vec(node->translation[0]*GLTF_SCALE, -node->translation[2]*GLTF_SCALE, node->translation[1]*GLTF_SCALE);
                            if(node->has_rotation)
                                q = quat(-node->rotation[0], node->rotation[2], -node->rotation[1], node->rotation[3]);
                            else
                            {
                                // No TRS: fall back to the local matrix.
                                cgltf_float m[16];
                                cgltf_node_transform_local(node, m);
                                pos = vec(m[12]*GLTF_SCALE, -m[14]*GLTF_SCALE, m[13]*GLTF_SCALE);
                                matrix3x3 rot;
                                // glTF m[] is column-major. Change of basis
                                // M: (x,y,z)->(x,-z,y). Guide's IBM swizzle,
                                // transposed into the engine's ROW storage:
                                // rows are (m[0],-m[8],m[4]),
                                // (-m[2],m[10],-m[6]), (m[1],-m[9],m[5]).
                                rot.a = vec(m[0], -m[8], m[4]);
                                rot.b = vec(-m[2], m[10], -m[6]);
                                rot.c = vec(m[1], -m[9], m[5]);
                                q = quat(rot);
                            }
                            q.normalize();
                            b.base = dualquat(q, pos);
                            if(b.parent >= 0) b.base.mul(skel->bones[b.parent].base, dualquat(b.base));
                            (b.invbase = b.base).invert();
                        }
                    }
                }
                if(skel->shared <= 1)
                    skel->linkchildren();
                printf("[DIAG] loadgltfmesh '%s': numbones=%d shared=%d firstbone='%s'\n", filename, skel->numbones, skel->shared, (skel->numbones > 0 && skel->bones[0].name) ? skel->bones[0].name : "(none)");
                fflush(stdout);

                // Per-model inverse bind pose from the skin.
                if(skin->inverse_bind_matrices)
                {
                    numinvbind = (int)skin->joints_count;
                    invbind = new dualquat[numinvbind];
                    float *ibm = new float[numinvbind*16];
                    if(readfloats(skin->inverse_bind_matrices, ibm, numinvbind, 16))
                    {
                        loopi(numinvbind)
                        {
                            float *mm = &ibm[i*16];
                            // Column-major 4x4 inverse bind matrix. Convert
                            // Y-up to engine space (negate Y in translation
                            // and in the rotation rows/cols as for joints).
                            vec pos(mm[12]*GLTF_SCALE, -mm[14]*GLTF_SCALE, mm[13]*GLTF_SCALE);
                            matrix3x3 rot;
                            // glTF mm[] is column-major. Change of basis
                            // M: (x,y,z)->(x,-z,y). Guide's IBM swizzle
                            // (M*IBM*M^-1), transposed into the engine's
                            // ROW storage: rows are (mm[0],-mm[8],mm[4]),
                            // (-mm[2],mm[10],-mm[6]), (mm[1],-mm[9],mm[5]).
                            rot.a = vec(mm[0], -mm[8], mm[4]);
                            rot.b = vec(-mm[2], mm[10], -mm[6]);
                            rot.c = vec(mm[1], -mm[9], mm[5]);
                            quat q(rot);
                            q.normalize();
                            invbind[i] = dualquat(q, pos);
                        }
                    }
                    delete[] ibm;
                }
            }

            // Per-model bind-pose correction: corr[b] = skel->bones[b].base
            // * invbind[b]. Applied to the skeleton's bone palette at render
            // so this model's vertices (authored in its own bind pose) skin
            // correctly under the shared skeleton's animation.
            if(invbind && numinvbind > 0 && skel && skel->bones && skel->numbones >= numinvbind)
            {
                numbindcorr = numinvbind;
                bindcorr = new dualquat[numbindcorr];
                loopi(numbindcorr)
                {
                    bindcorr[i] = skel->bones[i].base;
                    bindcorr[i].mul(invbind[i]);
                }
            }

            // Build meshes from glTF mesh primitives.
            loopi(data->meshes_count)
            {
                cgltf_mesh &gmesh = data->meshes[i];
                loopj(gmesh.primitives_count)
                {
                    cgltf_primitive &prim = gmesh.primitives[j];
                    if(prim.type != cgltf_primitive_type_triangles) continue;
                    cgltf_accessor *posacc = NULL, *normacc = NULL, *tcacc = NULL;
                    cgltf_accessor *jointacc = NULL, *weightacc = NULL;
                    loopk(prim.attributes_count)
                    {
                        cgltf_attribute &attr = prim.attributes[k];
                        if(attr.type == cgltf_attribute_type_position) posacc = attr.data;
                        else if(attr.type == cgltf_attribute_type_normal) normacc = attr.data;
                        else if(attr.type == cgltf_attribute_type_texcoord) tcacc = attr.data;
                        else if(attr.type == cgltf_attribute_type_joints) jointacc = attr.data;
                        else if(attr.type == cgltf_attribute_type_weights) weightacc = attr.data;
                    }
                    if(!posacc || !prim.indices) continue;
                    skelmesh *m = new skelmesh;
                    m->group = this;
                    meshes.add(m);
                    m->name = newstring(gmesh.name ? gmesh.name : "");
                    m->numverts = (int)posacc->count;
                    if(m->numverts)
                    {
                        m->verts = new vert[m->numverts];
                        float *pos = new float[m->numverts*3];
                        float *norm = normacc ? new float[m->numverts*3] : NULL;
                        float *tc = tcacc ? new float[m->numverts*2] : NULL;
                        if(!readfloats(posacc, pos, m->numverts, 3)) { delete[] pos; delete[] norm; delete[] tc; continue; }
                        if(normacc && !readfloats(normacc, norm, m->numverts, 3)) { delete[] norm; norm = NULL; }
                        if(tcacc && !readfloats(tcacc, tc, m->numverts, 2)) { delete[] tc; tc = NULL; }
                        // Skinning weights/bones (JOINTS_0 / WEIGHTS_0).
                        uint *joints = NULL;
                        float *weights = NULL;
                        int noblend = -1;
                        if(jointacc && weightacc)
                        {
                            joints = new uint[m->numverts*4];
                            weights = new float[m->numverts*4];
                            // JOINTS_0 is VEC4, NOT a scalar index buffer:
                            // cgltf_accessor_unpack_indices advances by the
                            // ELEMENT stride per scalar, so it reads 4x past
                            // the end of the joint data and segfaults (this
                            // crashed .82/.83/.84/.85 on the first glTF load).
                            // Read one VEC4 element per vertex instead, and
                            // clamp any joint index to the skeleton so a bad
                            // file can never hand addweight an out-of-range
                            // bone.
                            bool jointsok = true;
                            loopi(m->numverts)
                            {
                                if(!cgltf_accessor_read_uint(jointacc, i, &joints[i*4], 4)) { jointsok = false; break; }
                                loopk(4) if((int)joints[i*4+k] >= skel->numbones) joints[i*4+k] = 0;
                            }
                            // DIAG .91: what did the engine actually read for JOINTS_0?
                            if(jointsok && m->numverts >= 3)
                            {
                                printf("[DIAG] joints '%s': compType=%d stride=%d v0=[%u %u %u %u] w0=[%.2f %.2f %.2f %.2f] v1=[%u %u %u %u] v2=[%u %u %u %u]\n", m->name, (int)jointacc->component_type, (int)jointacc->stride, joints[0], joints[1], joints[2], joints[3], weights[0], weights[1], weights[2], weights[3], joints[4], joints[5], joints[6], joints[7], joints[8], joints[9], joints[10], joints[11]);
                                fflush(stdout);
                            }
                            if(!jointsok || !readfloats(weightacc, weights, m->numverts, 4))
                            {
                                delete[] joints; joints = NULL;
                                delete[] weights; weights = NULL;
                                blendcombo c;
                                c.finalize(0);
                                noblend = m->addblendcombo(c);
                            }
                        }
                        else
                        {
                            blendcombo c;
                            c.finalize(0);
                            noblend = m->addblendcombo(c);
                        }
                        loopi(m->numverts)
                        {
                            vert &v = m->verts[i];
                            // glTF Y-up -> engine Z-up (negate Y).
                            v.pos = vec(pos[i*3]*GLTF_SCALE, -pos[i*3+2]*GLTF_SCALE, pos[i*3+1]*GLTF_SCALE);
                            if(tc) { v.u = tc[i*2]; v.v = tc[i*2+1]; }
                            else v.u = v.v = 0;
                            if(norm) v.norm = vec(norm[i*3], -norm[i*3+2], norm[i*3+1]);
                            else v.norm = vec(0, 0, 0);
                            if(noblend < 0)
                            {
                                blendcombo c;
                                int sorted = 0;
                                loopk(4)
                                {
                                    float w = weights[i*4+k];
                                    int b = (int)joints[i*4+k];
                                    if(w > 0) sorted = c.addweight(sorted, w, b);
                                }
                                c.finalize(sorted);
                                v.blend = m->addblendcombo(c);
                            }
                            else v.blend = noblend;
                        }
                        delete[] pos; delete[] norm; delete[] tc;
                        delete[] joints; delete[] weights;
                    }
                    // Triangles.
                    m->numtris = (int)prim.indices->count/3;
                    if(m->numtris) m->tris = new tri[m->numtris];
                    uint *idx = new uint[prim.indices->count];
                    readindices(prim.indices, idx, (int)prim.indices->count);
                    loopj(m->numtris)
                    {
                        tri &t = m->tris[j];
                        t.vert[0] = (ushort)idx[j*3];
                        t.vert[1] = (ushort)idx[j*3+2];
                        t.vert[2] = (ushort)idx[j*3+1];
                    }
                    delete[] idx;
                    if(!m->numtris || !m->numverts)
                    {
                        conoutf("empty mesh in %s", filename);
                        meshes.removeobj(m);
                        delete m;
                    }
                }
            }

            sortblendcombos();
            return meshes.length() > 0;
        }

        bool loadmesh(const char *filename)
        {
            name = newstring(filename);
            cgltf_options options;
            memset(&options, 0, sizeof(options));
            cgltf_data *data = NULL;
            cgltf_result res = cgltf_parse_file(&options, filename, &data);
            if(res != cgltf_result_success || !data) return false;
            res = cgltf_load_buffers(&options, data, filename);
            if(res != cgltf_result_success)
            {
                cgltf_free(data);
                return false;
            }
            bool ok = loadgltfmesh(data, filename);
            cgltf_free(data);
            return ok;
        }

        // Interpolate a vec3 (translation/scale) sampler at time t.
        static bool sample_vec3(cgltf_animation_sampler *s, float t, vec &out)
        {
            if(!s || !s->input || !s->output) return false;
            int n = (int)s->input->count;
            if(n <= 0) return false;
            float *times = new float[n];
            float *vals = new float[n*3];
            if(cgltf_accessor_unpack_floats(s->input, times, n) == 0 ||
               cgltf_accessor_unpack_floats(s->output, vals, n*3) == 0)
            { delete[] times; delete[] vals; return false; }
            bool ok = true;
            if(t <= times[0]) out = vec(vals[0], vals[1], vals[2]);
            else if(t >= times[n-1]) out = vec(vals[(n-1)*3], vals[(n-1)*3+1], vals[(n-1)*3+2]);
            else if(s->interpolation == cgltf_interpolation_type_step)
            {
                int i = 0;
                while(i < n-1 && times[i+1] <= t) i++;
                out = vec(vals[i*3], vals[i*3+1], vals[i*3+2]);
            }
            else
            {
                int i = 0;
                while(i < n-1 && times[i+1] < t) i++;
                float f = (t - times[i]) / max(times[i+1] - times[i], 1e-6f);
                out = vec(vals[i*3] + (vals[(i+1)*3] - vals[i*3])*f,
                          vals[i*3+1] + (vals[(i+1)*3+1] - vals[i*3+1])*f,
                          vals[i*3+2] + (vals[(i+1)*3+2] - vals[i*3+2])*f);
            }
            delete[] times; delete[] vals;
            return ok;
        }

        // Interpolate a rotation (quat) sampler at time t (nlerp).
        static bool sample_quat(cgltf_animation_sampler *s, float t, quat &out)
        {
            if(!s || !s->input || !s->output) return false;
            int n = (int)s->input->count;
            if(n <= 0) return false;
            float *times = new float[n];
            float *vals = new float[n*4];
            if(cgltf_accessor_unpack_floats(s->input, times, n) == 0 ||
               cgltf_accessor_unpack_floats(s->output, vals, n*4) == 0)
            { delete[] times; delete[] vals; return false; }
            if(t <= times[0]) out = quat(vals[0], vals[1], vals[2], vals[3]);
            else if(t >= times[n-1]) out = quat(vals[(n-1)*4], vals[(n-1)*4+1], vals[(n-1)*4+2], vals[(n-1)*4+3]);
            else if(s->interpolation == cgltf_interpolation_type_step)
            {
                int i = 0;
                while(i < n-1 && times[i+1] <= t) i++;
                out = quat(vals[i*4], vals[i*4+1], vals[i*4+2], vals[i*4+3]);
            }
            else
            {
                int i = 0;
                while(i < n-1 && times[i+1] < t) i++;
                float f = (t - times[i]) / max(times[i+1] - times[i], 1e-6f);
                // nlerp (normalize after lerp); glTF quats are (x,y,z,w).
                quat q(vals[i*4] + (vals[(i+1)*4] - vals[i*4])*f,
                       vals[i*4+1] + (vals[(i+1)*4+1] - vals[i*4+1])*f,
                       vals[i*4+2] + (vals[(i+1)*4+2] - vals[i*4+2])*f,
                       vals[i*4+3] + (vals[(i+1)*4+3] - vals[i*4+3])*f);
                // Keep the shortest path (flip if the dot is negative).
                if(q.x*vals[i*4] + q.y*vals[i*4+1] + q.z*vals[i*4+2] + q.w*vals[i*4+3] < 0)
                { q.x = -q.x; q.y = -q.y; q.z = -q.z; q.w = -q.w; }
                q.normalize();
                out = q;
            }
            delete[] times; delete[] vals;
            return true;
        }

        // Sample one glTF animation into the skeleton's framebones and
        // register a skelanimspec. Samples at 30 fps over the animation's
        // duration. Joint local TRS comes from the animation channels;
        // model-space poses accumulate down the hierarchy (parents first),
        // then each frame is multiplied by the joint's invbase, mirroring
        // the IQM frame format (framebones = animated * invbase).
        bool loadgltfanim(cgltf_data *data, cgltf_animation &anim, const char *animname)
        {
            if(!skel || skel->numbones <= 0 || !skel->bones) return false;
            skelanimspec *sa = skel->findskelanim(animname);
            if(sa) return true;
            // Duration = max keyframe time over all samplers.
            float duration = 0;
            loopi(anim.samplers_count)
            {
                cgltf_accessor *in = anim.samplers[i].input;
                if(!in || in->count <= 0) continue;
                float *times = new float[in->count];
                if(cgltf_accessor_unpack_floats(in, times, (int)in->count) != 0)
                    duration = max(duration, times[in->count-1]);
                delete[] times;
            }
            if(duration <= 0) return false;
            const float fps = 30.0f;
            int numframes = max(1, (int)ceilf(duration * fps));
            // Map skin joint node -> bone index (bones are in skin order).
            // Build a node->bone lookup from the skeleton's bone names.
            // The animation channels target nodes; match by node pointer
            // via the skin joints. We use the first skin (as in the mesh).
            cgltf_skin *skin = data->skins_count > 0 ? &data->skins[0] : NULL;
            if(!skin) return false;
            // For each bone, find its translation/rotation/scale channels.
            // Precompute per-bone channel pointers.
            struct bonechan { cgltf_animation_sampler *trans, *rot, *scale; };
            bonechan *bc = new bonechan[skel->numbones];
            loopi(skel->numbones) { bc[i].trans = bc[i].rot = bc[i].scale = NULL; }
            loopi(anim.channels_count)
            {
                cgltf_animation_channel &ch = anim.channels[i];
                if(!ch.target_node || !ch.sampler) continue;
                // Find which bone this node is.
                int bone = -1;
                int nj = min((int)skin->joints_count, skel->numbones);
                loopj(nj) if(skin->joints[j] == ch.target_node) { bone = j; break; }
                if(bone < 0) continue;
                if(ch.target_path == cgltf_animation_path_type_translation) bc[bone].trans = ch.sampler;
                else if(ch.target_path == cgltf_animation_path_type_rotation) bc[bone].rot = ch.sampler;
                else if(ch.target_path == cgltf_animation_path_type_scale) bc[bone].scale = ch.sampler;
            }
            // Allocate frames.
            sa = &skel->addskelanim(animname);
            sa->frame = skel->numframes;
            sa->range = numframes;
            dualquat *animbones = new dualquat[(skel->numframes + numframes) * skel->numbones];
            if(skel->framebones && skel->numframes > 0)
            {
                memcpy(animbones, skel->framebones, skel->numframes * skel->numbones * sizeof(dualquat));
                delete[] skel->framebones;
            }
            skel->framebones = animbones;
            animbones += skel->numframes * skel->numbones;
            skel->numframes += numframes;
            // Sample each frame. anim_pose[] holds the model-space animated
            // transform per joint (without invbase); frame[] = anim_pose * invbase.
            dualquat *animpose = new dualquat[skel->numbones];
            // Base (bind) local transform per joint, for joints with no channel.
            loopi(numframes)
            {
                float t = i / fps;
                dualquat *frame = &animbones[i * skel->numbones];
                loopk(skel->numbones)
                {
                    boneinfo &b = skel->bones[k];
                    // Local TRS at time t. Default is the joint's bind-pose
                    // local transform from the glTF node; animation channels
                    // override it. (The bind local transform keeps joints
                    // with no channel in the bind pose.)
                    vec pos(0, 0, 0);
                    quat rot(0, 0, 0, 1);
                    if(skin->joints[k])
                    {
                        cgltf_node *node = skin->joints[k];
                        if(node->has_translation)
                            pos = vec(node->translation[0]*GLTF_SCALE, -node->translation[2]*GLTF_SCALE, node->translation[1]*GLTF_SCALE);
                        if(node->has_rotation)
                            // glTF quat (x,y,z,w) -> engine: negate x and z
                            // (same conversion as the IQM loader and as the
                            // joint matrix conversion in loadgltfmesh).
                            rot = quat(-node->rotation[0], node->rotation[2], -node->rotation[1], node->rotation[3]);
                    }
                    if(bc[k].trans) { vec p; if(sample_vec3(bc[k].trans, t, p)) pos = vec(p.x*GLTF_SCALE, -p.z*GLTF_SCALE, p.y*GLTF_SCALE); }
                    if(bc[k].rot)
                    {
                        quat q;
                        if(sample_quat(bc[k].rot, t, q))
                            rot = quat(-q.x, q.z, -q.y, q.w);
                    }
                    // Scale is ignored (these rigs are unscaled); the
                    // engine's dualquat has no scale component.
                    dualquat local(rot, pos);
                    // Scale: fold into the local transform (uniform-ish).
                    // The engine's dualquat has no scale; apply scale to
                    // the translation magnitude is wrong, so ignore
                    // non-uniform scale (these rigs are unscaled).
                    if(b.parent >= 0) animpose[k] = animpose[b.parent];
                    else animpose[k] = dualquat(quat(0,0,0,1), vec(0,0,0));
                    // animpose[k] = parent_animpose * local
                    {
                        dualquat p = animpose[k];
                        animpose[k].mul(p, local);
                    }
                    frame[k] = animpose[k];
                    frame[k].mul(skel->bones[k].invbase);
                    if(adjustments.inrange(k)) adjustments[k].adjust(frame[k]);
                    frame[k].fixantipodal(skel->framebones[k]);
                }
            }
            // DIAG .93: print frame 0 of the idle animation (the pose the
            // player stands in) so we can see whether the engine's bone
            // rotations match the source. For a correct idle these should
            // be near-identity with small translations.
            if(strstr(animname, "Idle_No_Loop"))
            {
                loopk(skel->numbones)
                {
                    const char *bn = skel->bones[k].name ? skel->bones[k].name : "?";
                    if(k <= 6 || !strcmp(bn, "thigh_l") || !strcmp(bn, "upperarm_l") || !strcmp(bn, "clavicle_l"))
                    {
                        dualquat &f = animbones[k];
                        vec tp = f.transform(vec(0, 0, 0));
                        printf("[DIAG] frame0 '%s' bone %d '%s': rot=(%.3f,%.3f,%.3f,%.3f) trans=(%.3f,%.3f,%.3f)\n", animname, k, bn, f.real.x, f.real.y, f.real.z, f.real.w, tp.x, tp.y, tp.z);
                    }
                }
                fflush(stdout);
            }
            delete[] animpose;
            delete[] bc;
            return true;
        }

        skelanimspec *loadanim(const char *animname)
        {
            // Anim name is "file.glb:AnimName" or "AnimName" (searches the
            // model's own anims.glb). Mirrors iqm's loadanim.
            const char *sep = strchr(animname, ':');
            skelanimspec *sa = skel->findskelanim(animname, sep ? '\0' : ':');
            if(sa) return sa;
            string filepath;
            const char *aname = animname;
            if(sep)
            {
                // "path/to/file.glb:AnimName" — setanim already supplies the
                // full path (dir + "/" + file), so use it as-is.
                copystring(filepath, animname, sep - animname + 1);
                aname = sep + 1;
            }
            else
            {
                // Bare animation name: look in anims.glb next to the model.
                // dir is "packages/models/<modeldir>" (set by gltfdir).
                formatstring(filepath)("%s/anims.glb", dir);
            }
            cgltf_options options;
            memset(&options, 0, sizeof(options));
            cgltf_data *data = NULL;
            if(cgltf_parse_file(&options, filepath, &data) != cgltf_result_success || !data) return NULL;
            if(cgltf_load_buffers(&options, data, filepath) != cgltf_result_success)
            {
                cgltf_free(data);
                return NULL;
            }
            sa = NULL;
            loopi(data->animations_count)
            {
                cgltf_animation &anim = data->animations[i];
                if(anim.name && !strcmp(anim.name, aname))
                {
                    if(loadgltfanim(data, anim, animname)) sa = skel->findskelanim(animname);
                    break;
                }
            }
            cgltf_free(data);
            return sa;
        }
    };

    meshgroup *loadmeshes(const char *name, va_list args)
    {
        gltfmeshgroup *group = new gltfmeshgroup;
        group->shareskeleton(va_arg(args, char *));
        if(!group->loadmesh(name)) { delete group; return NULL; }
        return group;
    }

    bool loaddefaultparts()
    {
        skelpart &mdl = *new skelpart;
        parts.add(&mdl);
        mdl.model = this;
        mdl.index = 0;
        mdl.pitchscale = mdl.pitchoffset = mdl.pitchmin = mdl.pitchmax = 0;
        adjustments.setsize(0);
        const char *fname = loadname + strlen(loadname);
        do --fname; while(fname >= loadname && *fname!='/' && *fname!='\\'); fname++;
        defformatstring(meshname)("packages/models/%s/%s.gltf", loadname, fname);
        // Try .gltf first, then .glb. Share the "humanoid" skeleton so
        // all models on the 66-joint Humanoid rig share one skeleton
        // object (enables LINK_REUSE for armor attaches); the per-model
        // bind-pose correction (bindcorr) handles bind-pose differences.
        mdl.meshes = sharemeshes(path(meshname), (char *)"humanoid");
        if(!mdl.meshes)
        {
            defformatstring(glbname)("packages/models/%s/%s.glb", loadname, fname);
            mdl.meshes = sharemeshes(path(glbname), NULL);
        }
        if(!mdl.meshes) return false;
        mdl.initanimparts();
        mdl.initskins();
        return true;
    }

    bool load()
    {
        if(loaded) return true;
        formatstring(dir)("packages/models/%s", loadname);
        defformatstring(cfgname)("packages/models/%s/gltf.cfg", loadname);

        loading = this;
        identflags &= ~IDF_PERSIST;
        if(execfile(cfgname, false) && parts.length())
        {
            identflags |= IDF_PERSIST;
            loading = NULL;
            loopv(parts) if(!parts[i]->meshes) return false;
        }
        else
        {
            identflags |= IDF_PERSIST;
            if(!loaddefaultparts())
            {
                loading = NULL;
                return false;
            }
            loading = NULL;
        }
        scale /= 4;
        parts[0]->translate = translate;
        loopv(parts)
        {
            skelpart *p = (skelpart *)parts[i];
            p->endanimparts();
            p->meshes->shared++;
        }
        return loaded = true;
    }
};

skelcommands<gltf> gltfcommands;
