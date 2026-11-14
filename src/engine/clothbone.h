// Cape cloth simulation — Verlet bone solver for the rigged cape.
// Adapted from the "Dynamic Cape Physics for IQM Player Models" guide
// (Cube2_IQM_Cape_Physics_Guide_0_ipj4.pdf), Option A (Verlet, zero deps).
//
// The cape is a rigged IQM attach model (21 joints: root + 4 chains of 5
// bones, ch1_b1..ch4_b5). It carries a 1-frame identity animation so it
// goes through the normal animated path (checkskelcache -> interpbones ->
// bdata) instead of rendering as a static bind-pose VBO. After interpbones
// computes bdata, cape_update() (called from skelmeshgroup::render) runs
// the Verlet solver in WORLD space and overwrites the cape bone DualQuats
// in rsc.bdata, so both CPU and GPU skinning use the simulated pose.
//
// Scale: the guide assumes 1 m = 8 units. Shattered Shield uses
// ~35.8 units/m (humanoids ~60-65 units tall), so linear constants are
// rescaled by CAPE_SCALE = 35.8/8 = 4.475. Damping and iteration count
// are dimensionless and unchanged.
//
// Simulation is in world space so character motion produces real inertia:
// the pinned top row follows the character (via the world matrix) while
// the free joints lag behind on their previous positions. Gravity is
// world-down (0,0,-G). The torso collision is a capsule along the spine
// whose endpoints are fixed offsets in the cape's model space (the torso
// sits at a fixed place relative to the cape, since the cape is attached
// to the character), so they are transformed to world space each frame.

#pragma once

// ---------------------------------------------------------------- constants
// Units: Shattered Shield game units. 1 m ~= 35.8 units.
#define CAPE_SCALE 4.475f

// Gravity magnitude, world units/s^2. Guide: 98 (at 8 units/m = 12.25 m/s^2);
// rescaled: 98 * 4.475 = 438.6.
#define CAPE_GRAVITY 438.6f
// Verlet damping (guide 0.96, dimensionless).
#define CAPE_DAMPING 0.96f
// Solver iterations per frame (guide 4).
#define CAPE_ITERATIONS 4
// Max dt the solver accepts per step (guide 0.1 s); larger steps clamp.
#define CAPE_MAX_DT 0.1f
// Torso collision capsule (replaces the guide's single sphere, which did
// not follow the back's shape). The capsule runs along the spine in the
// cape's model space: Y is down the back (neck Y~0, hem Y~-47), Z is
// toward/away from the body (the torso sits at negative Z). A is the top
// (neck/shoulders), B the bottom (lower back); the radius is the torso's
// half-depth. Tune these if the cape clips or floats.
// Radius: guide 4.5 at 8 units/m -> 20.1 rescaled; 17.0 chosen so the
// capsule's back surface (Z = -18 + 17 = -1) sits where the sphere's did.
#define CAPE_TORSO_RADIUS 17.0f
#define CAPE_TORSO_AX 0.0f
#define CAPE_TORSO_AY -8.0f
#define CAPE_TORSO_AZ -18.0f
#define CAPE_TORSO_BX 0.0f
#define CAPE_TORSO_BY -34.0f
#define CAPE_TORSO_BZ -18.0f

// Max caped entities simulated at once (one state per dynent).
#define CAPE_MAX_STATES 32

struct CapeJoint
{
    int boneidx;    // interpindex into bdata (skeleton bone slot)
    int parentidx;  // index into the joints array, -1 for the root joint
    vec pos;        // current world-space position
    vec oldpos;     // previous-frame world-space position
    float restlen;  // distance to parent joint in bind pose
    bool pinned;    // true for the top row (root + ch*_b1): follows the body
};

struct CapeSimInstance
{
    dynent *owner;              // dynent this state belongs to, NULL if free
    vector<CapeJoint> joints;
    vector<vec> restpos;        // bind-pose joint positions, model space
    vector<quat> restorient;    // bind-pose joint orientations, model space
    vector<vec> restaxis;       // bind-pose chain direction per joint, model space
    vector<int> childidx;       // first child joint index per joint, -1 for leaves
    bool initialized;
    int lastmillis;

    CapeSimInstance() : owner(NULL), initialized(false), lastmillis(0) {}
};

// Rotation taking unit vector 'from' onto unit vector 'to'.
// Standard (cross, 1+dot) formulation, with a guard for opposite vectors.
static inline quat cape_quatfromvectors(const vec &from, const vec &to)
{
    vec f = vec(from).normalize();
    vec t = vec(to).normalize();
    float d = f.dot(t);
    if(d > 0.999999f) return quat(0, 0, 0, 1);
    vec cr;
    cr.cross(f, t);
    if(d < -0.999999f)
    {
        // Opposite vectors: rotate 180 degrees about any perpendicular axis.
        vec axis = fabs(f.x) < 0.9f ? vec(1, 0, 0) : vec(0, 1, 0);
        axis = vec().cross(f, axis).normalize();
        return quat(axis, PI);
    }
    float inv = 1.0f / sqrtf(2.0f * (1.0f + d));
    return quat(cr.x * inv, cr.y * inv, cr.z * inv, (1.0f + d) * inv);
}

// Per-dynent solver states (function-local static so this header can be
// included inside struct skelmodel, where a file-scope static array would
// become a static member needing an out-of-line definition).
static CapeSimInstance *cape_getstate(dynent *d)
{
    static CapeSimInstance cape_states[CAPE_MAX_STATES];
    loopi(CAPE_MAX_STATES) if(cape_states[i].owner == d) return &cape_states[i];
    loopi(CAPE_MAX_STATES) if(!cape_states[i].owner)
    {
        cape_states[i].owner = d;
        return &cape_states[i];
    }
    // No free slot: reuse the first (oldest) state.
    cape_states[0] = CapeSimInstance();
    cape_states[0].owner = d;
    return &cape_states[0];
}

// True if this skeleton is the rigged cape (bone ch1_b1 present).
static bool cape_iscape(skeleton *skel)
{
    if(!skel || !skel->bones) return false;
    loopi(skel->numbones) if(skel->bones[i].name && !strcmp(skel->bones[i].name, "ch1_b1")) return true;
    return false;
}

// Closest point on segment AB to point P (for the capsule collision).
static vec cape_closestpointonsegment(const vec &p, const vec &a, const vec &b)
{
    vec ab = vec(b).sub(a);
    float len2 = ab.squaredlen();
    if(len2 < 1e-8f) return a;
    float t = vec(p).sub(a).dot(ab) / len2;
    if(t < 0.0f) t = 0.0f;
    else if(t > 1.0f) t = 1.0f;
    return vec(a).add(vec(ab).mul(t));
}

// Step the Verlet solver in world space.
// dt <= 0 means no time passed (same-frame re-render): do nothing, so the
// solver is never stepped twice for one frame. dt above the cap clamps.
static void cape_step(CapeSimInstance &cape, float dt, const vec &gravity, const vec &torsoA, const vec &torsoB, float torsoRadius)
{
    if(dt <= 0.0f) return;
    if(dt > CAPE_MAX_DT) dt = CAPE_MAX_DT;
    float dt2 = dt * dt;

    // 1. Verlet position integration (free joints only).
    loopv(cape.joints)
    {
        CapeJoint &j = cape.joints[i];
        if(j.pinned) continue;
        vec vel = vec(j.pos).sub(j.oldpos).mul(CAPE_DAMPING);
        j.oldpos = j.pos;
        j.pos.add(vel).add(vec(gravity).mul(dt2));
    }

    // 2. Distance constraints + torso collision (several iterations).
    loopk(CAPE_ITERATIONS)
    {
        loopv(cape.joints)
        {
            CapeJoint &j = cape.joints[i];
            if(j.pinned || j.parentidx < 0) continue;
            CapeJoint &p = cape.joints[j.parentidx];
            vec delta = vec(j.pos).sub(p.pos);
            float dist = delta.magnitude();
            if(dist > 1e-4f)
            {
                float diff = (dist - j.restlen) / dist;
                if(p.pinned) j.pos.sub(vec(delta).mul(diff));
                else
                {
                    vec corr = vec(delta).mul(0.5f * diff);
                    j.pos.sub(corr);
                    p.pos.add(corr);
                }
            }
            // Capsule collision: push the joint out of the torso capsule.
            vec closest = cape_closestpointonsegment(j.pos, torsoA, torsoB);
            vec toJoint = vec(j.pos).sub(closest);
            float bodyDist = toJoint.magnitude();
            if(bodyDist < torsoRadius && bodyDist > 1e-4f)
                j.pos = vec(closest).add(toJoint.mul(torsoRadius / bodyDist));
        }
    }
}

// Run the cape solver for this render and overwrite the cape bone
// DualQuats in bdata (model space). Called from skelmeshgroup::render
// after rsc (the corrected bdata) is available, before skinning.
//   skel      - the cape skeleton (checked by the caller via cape_iscape)
//   bdata     - rsc.bdata, numinterpbones DualQuats, model space
//   worldmat  - cape model-to-world matrix (animmodel::matrixstack[matrixpos])
//   d         - the dynent wearing the cape (per-entity solver state)
static void cape_update(skeleton *skel, dualquat *bdata, const matrix3x4 &worldmat, dynent *d)
{
    if(!skel || !bdata || !d || skel->numbones > 64) return;
    CapeSimInstance &cape = *cape_getstate(d);
    bool firstframe = !cape.initialized;

    // First run: build the joint list from the skeleton and capture the
    // bind pose (the 1-frame identity animation means bdata IS the bind
    // pose right now, before any solver override).
    if(firstframe)
    {
        cape.joints.shrink(0);
        cape.restpos.shrink(0);
        cape.restorient.shrink(0);
        cape.restaxis.shrink(0);
        cape.childidx.shrink(0);
        // Map bone index -> joint index.
        int bone2joint[64];
        loopi(64) bone2joint[i] = -1;
        loopi(skel->numbones)
        {
            boneinfo &b = skel->bones[i];
            if(b.interpindex < 0) continue;
            CapeJoint j;
            j.boneidx = b.interpindex;
            j.parentidx = -1;
            j.restlen = 0;
            j.pos = j.oldpos = vec(0, 0, 0);
            // Pinned: the root (no parent) and the top bone of each chain
            // (ch*_b1: name "chNb1"). These follow the neck/shoulders;
            // everything below simulates.
            j.pinned = (b.parent < 0) ||
                (b.name && strlen(b.name) == 6 && b.name[0] == 'c' && b.name[1] == 'h' &&
                 b.name[3] == '_' && b.name[4] == 'b' && b.name[5] == '1');
            // Model-space bind position of this bone.
            vec mpos = bdata[b.interpindex].transform(vec(0, 0, 0));
            cape.restpos.add(mpos);
            cape.restorient.add(bdata[b.interpindex].real);
            cape.restaxis.add(vec(0, 0, 0));
            cape.childidx.add(-1);
            bone2joint[i] = cape.joints.length();
            cape.joints.add(j);
        }
        // Link parents, rest lengths, first children.
        loopi(skel->numbones)
        {
            boneinfo &b = skel->bones[i];
            if(b.interpindex < 0 || bone2joint[i] < 0) continue;
            int ji = bone2joint[i];
            if(b.parent >= 0 && bone2joint[b.parent] >= 0)
            {
                int pj = bone2joint[b.parent];
                cape.joints[ji].parentidx = pj;
                cape.joints[ji].restlen = cape.restpos[ji].dist(cape.restpos[pj]);
                if(cape.childidx[pj] < 0) cape.childidx[pj] = ji;
            }
        }
        // Rest chain direction per joint (model space): toward first child,
        // or from parent for leaves.
        loopv(cape.joints)
        {
            int c = cape.childidx[i];
            if(c >= 0) cape.restaxis[i] = vec(cape.restpos[c]).sub(cape.restpos[i]).normalize();
            else if(cape.joints[i].parentidx >= 0)
                cape.restaxis[i] = vec(cape.restpos[i]).sub(cape.restpos[cape.joints[i].parentidx]).normalize();
            else cape.restaxis[i] = vec(0, 0, -1);
        }
        cape.initialized = true;
        cape.lastmillis = lastmillis;
    }

    float dt = (lastmillis - cape.lastmillis) / 1000.0f;
    cape.lastmillis = lastmillis;

    // Sync joint world positions: pinned joints follow the animated
    // skeleton (bind pose via the world matrix); free joints are seeded
    // at the bind pose on the first frame and then keep their simulated
    // positions.
    loopv(cape.joints)
    {
        CapeJoint &j = cape.joints[i];
        vec worldPos = worldmat.transform(cape.restpos[i]);
        if(j.pinned) j.pos = j.oldpos = worldPos;
        else if(firstframe) j.pos = j.oldpos = worldPos;
    }

    // Step physics in world space.
    vec torsoAWorld = worldmat.transform(vec(CAPE_TORSO_AX, CAPE_TORSO_AY, CAPE_TORSO_AZ));
    vec torsoBWorld = worldmat.transform(vec(CAPE_TORSO_BX, CAPE_TORSO_BY, CAPE_TORSO_BZ));
    cape_step(cape, dt, vec(0, 0, -CAPE_GRAVITY), torsoAWorld, torsoBWorld, CAPE_TORSO_RADIUS);

    // Write the simulated pose back into bdata (model space).
    // For each joint: model-space position + orientation rotated from the
    // rest chain direction onto the simulated chain direction.
    loopv(cape.joints)
    {
        CapeJoint &j = cape.joints[i];
        vec localPos = worldmat.transposedtransform(j.pos);
        // Simulated chain direction in model space.
        vec simdir;
        int c = cape.childidx[i];
        if(c >= 0)
        {
            vec childLocal = worldmat.transposedtransform(cape.joints[c].pos);
            simdir = vec(childLocal).sub(localPos);
        }
        else if(j.parentidx >= 0)
        {
            vec parentLocal = worldmat.transposedtransform(cape.joints[j.parentidx].pos);
            simdir = vec(localPos).sub(parentLocal);
        }
        else continue;
        if(simdir.magnitude() < 1e-4f) continue;
        simdir.normalize();
        quat rot = cape_quatfromvectors(cape.restaxis[i], simdir);
        quat neworient = quat(rot).mul(cape.restorient[i]);
        bdata[j.boneidx] = dualquat(neworient, localPos);
        bdata[j.boneidx].normalize();
    }
}
