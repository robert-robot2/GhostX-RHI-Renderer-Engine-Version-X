// GhostRoleEdit.cs - Details > Gameplay > Behavior groups for Role = Player and Role = Enemy
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Windows.Forms;

namespace GhostShell
{
    internal static class GhostRoleParamCheck
    {
        private static bool _done;
        public static void Verify()      // catches a C# struct that no longer matches GhostRoleParams.h
        {
            if (_done) return;
            _done = true;
            if (Marshal.SizeOf<GhostPlayerParams>() != 49 * 4 || Marshal.SizeOf<GhostEnemyParams>() != 25 * 4 ||
       Marshal.SizeOf<GhostBossParams>() != 12 * 4 || Marshal.SizeOf<GhostPathParams>() != 4 * 4)
                MessageBox.Show("GhostPlayerParams / GhostEnemyParams in GhostEngine.cs do not match GhostRoleParams.h (field count). " +
                    "Role settings will be wrong until they match.", "Role settings", MessageBoxButtons.OK, MessageBoxIcon.Error);
        }
    }

    // ---------------- Player ----------------
    internal abstract class PlayerGroup
    {
        private readonly int _id;
        protected delegate void Mut(ref GhostPlayerParams p);
        protected PlayerGroup(int id) { _id = id; }
        protected GhostPlayerParams Get() { var p = new GhostPlayerParams(); GhostEngine.GhostEngine_GetPlayerParams(_id, ref p); return p; }
        protected void Edit(Mut m) { var p = Get(); m(ref p); GhostEngine.GhostEngine_SetPlayerParams(_id, ref p); }
        public override string ToString() => "";
    }

    [TypeConverter(typeof(ExpandableObjectConverter))]
    internal sealed class PlayerMoveGroup : PlayerGroup
    {
        public PlayerMoveGroup(int id) : base(id) { }
        [DisplayName("Walk Speed (units/s)")] public float Walk { get => Get().walk; set => Edit((ref GhostPlayerParams p) => p.walk = value); }
        [DisplayName("Run Speed (units/s)")] public float Run { get => Get().run; set => Edit((ref GhostPlayerParams p) => p.run = value); }
        [DisplayName("Run Animation Speed (sprites)")] public float RunAnimSpeed { get => Get().runAnimSpeed; set => Edit((ref GhostPlayerParams p) => p.runAnimSpeed = value); }
        [DisplayName("Ground Accel")] public float GroundAccel { get => Get().groundAccel; set => Edit((ref GhostPlayerParams p) => p.groundAccel = value); }
        [DisplayName("Air Accel")] public float AirAccel { get => Get().airAccel; set => Edit((ref GhostPlayerParams p) => p.airAccel = value); }
        [DisplayName("Gravity")] public float Gravity { get => Get().gravity; set => Edit((ref GhostPlayerParams p) => p.gravity = value); }
        [DisplayName("Max Fall Speed")] public float MaxFall { get => Get().maxFall; set => Edit((ref GhostPlayerParams p) => p.maxFall = value); }
        [DisplayName("Fall Limit (Y: below this = dies)")] public float FallLimit { get => Get().fallLimit; set => Edit((ref GhostPlayerParams p) => p.fallLimit = value); }
        [DisplayName("Side-Scroller (no depth movement)")] public bool SideScroller { get => Get().sideScroller != 0; set => Edit((ref GhostPlayerParams p) => p.sideScroller = value ? 1 : 0); }
    }

    [TypeConverter(typeof(ExpandableObjectConverter))]
    internal sealed class PlayerJumpGroup : PlayerGroup
    {
        public PlayerJumpGroup(int id) : base(id) { }
        [DisplayName("Jump Speed")] public float Jump { get => Get().jump; set => Edit((ref GhostPlayerParams p) => p.jump = value); }
        [DisplayName("Run Jump Speed (sprinting)")] public float RunJump { get => Get().runJump; set => Edit((ref GhostPlayerParams p) => p.runJump = value); }
        [DisplayName("Jump Cut (on release, 0-1)")] public float JumpCut { get => Get().jumpCut; set => Edit((ref GhostPlayerParams p) => p.jumpCut = value); }
        [DisplayName("Coyote Time (s)")] public float CoyoteTime { get => Get().coyoteTime; set => Edit((ref GhostPlayerParams p) => p.coyoteTime = value); }
        [DisplayName("Jump Buffer (s)")] public float JumpBuffer { get => Get().jumpBuffer; set => Edit((ref GhostPlayerParams p) => p.jumpBuffer = value); }
    }

    [TypeConverter(typeof(ExpandableObjectConverter))]
    internal sealed class PlayerDamageGroup : PlayerGroup
    {
        public PlayerDamageGroup(int id) : base(id) { }
        [DisplayName("Stomp Bounce")] public float StompBounce { get => Get().stompBounce; set => Edit((ref GhostPlayerParams p) => p.stompBounce = value); }
        [DisplayName("Stomp Bounce (jump held)")] public float StompBounceHeld { get => Get().stompBounceHeld; set => Edit((ref GhostPlayerParams p) => p.stompBounceHeld = value); }
        [DisplayName("Stomp Tolerance (units)")] public float StompTolerance { get => Get().stompTolerance; set => Edit((ref GhostPlayerParams p) => p.stompTolerance = value); }
        [DisplayName("Invulnerable Time (s)")] public float Invulnerable { get => Get().invulnerable; set => Edit((ref GhostPlayerParams p) => p.invulnerable = value); }
        [DisplayName("Hurt Pose Time (s, no Shrink anim)")] public float HurtTime { get => Get().hurtTime; set => Edit((ref GhostPlayerParams p) => p.hurtTime = value); }
        [DisplayName("Hurt Hop (no Shrink anim)")] public float HurtHop { get => Get().hurtHop; set => Edit((ref GhostPlayerParams p) => p.hurtHop = value); }
        [DisplayName("Small Player Breaks Bricks")] public bool SmallBreaksBricks { get => Get().smallBreaksBricks != 0; set => Edit((ref GhostPlayerParams p) => p.smallBreaksBricks = value ? 1 : 0); }
    }

    [TypeConverter(typeof(ExpandableObjectConverter))]
    internal sealed class PlayerPowerGroup : PlayerGroup
    {
        public PlayerPowerGroup(int id) : base(id) { }
        [DisplayName("Big Width (x)")] public float BigScaleX { get => Get().bigScaleX; set => Edit((ref GhostPlayerParams p) => p.bigScaleX = value); }
        [DisplayName("Big Height (x)")] public float BigScaleY { get => Get().bigScaleY; set => Edit((ref GhostPlayerParams p) => p.bigScaleY = value); }
        [DisplayName("Fire Width (x)")] public float FireScaleX { get => Get().fireScaleX; set => Edit((ref GhostPlayerParams p) => p.fireScaleX = value); }
        [DisplayName("Fire Height (x)")] public float FireScaleY { get => Get().fireScaleY; set => Edit((ref GhostPlayerParams p) => p.fireScaleY = value); }
        [DisplayName("3D Big Scale (x)")] public float ModelBigScale { get => Get().modelBigScale; set => Edit((ref GhostPlayerParams p) => p.modelBigScale = value); }
        [DisplayName("3D Fire Scale (x)")] public float ModelFireScale { get => Get().modelFireScale; set => Edit((ref GhostPlayerParams p) => p.modelFireScale = value); }
        [DisplayName("Grow Pop (lift)")] public float GrowPop { get => Get().growPop; set => Edit((ref GhostPlayerParams p) => p.growPop = value); }
        [DisplayName("Transform Time (s)")] public float TransformTime { get => Get().transformTime; set => Edit((ref GhostPlayerParams p) => p.transformTime = value); }
        [DisplayName("Shrink Time (s)")] public float ShrinkTime { get => Get().shrinkTime; set => Edit((ref GhostPlayerParams p) => p.shrinkTime = value); }
        [DisplayName("Star Time (s)")] public float StarTime { get => Get().starTime; set => Edit((ref GhostPlayerParams p) => p.starTime = value); }
    }

    [TypeConverter(typeof(ExpandableObjectConverter))]
    internal sealed class PlayerSkidGroup : PlayerGroup
    {
        public PlayerSkidGroup(int id) : base(id) { }
        [DisplayName("Min Speed to Skid")] public float SkidMinSpeed { get => Get().skidMinSpeed; set => Edit((ref GhostPlayerParams p) => p.skidMinSpeed = value); }
        [DisplayName("Exit Speed")] public float SkidExitSpeed { get => Get().skidExitSpeed; set => Edit((ref GhostPlayerParams p) => p.skidExitSpeed = value); }
        [DisplayName("Friction (braking)")] public float SkidFriction { get => Get().skidFriction; set => Edit((ref GhostPlayerParams p) => p.skidFriction = value); }
        [DisplayName("Oppose Cos (-1 = exactly opposite)")] public float SkidOpposeCos { get => Get().skidOpposeCos; set => Edit((ref GhostPlayerParams p) => p.skidOpposeCos = value); }
    }

    [TypeConverter(typeof(ExpandableObjectConverter))]
    internal sealed class PlayerAttackGroup : PlayerGroup
    {
        public PlayerAttackGroup(int id) : base(id) { }
        [DisplayName("Attack Cooldown (s)")] public float AttackCooldown { get => Get().attackCooldown; set => Edit((ref GhostPlayerParams p) => p.attackCooldown = value); }
        [DisplayName("Fireball Speed")] public float FireballSpeed { get => Get().fireballSpeed; set => Edit((ref GhostPlayerParams p) => p.fireballSpeed = value); }
        [DisplayName("Fireball Life (s)")] public float FireballLife { get => Get().fireballLife; set => Edit((ref GhostPlayerParams p) => p.fireballLife = value); }
        [DisplayName("Fireball Bounce")] public float FireballBounce { get => Get().fireballBounce; set => Edit((ref GhostPlayerParams p) => p.fireballBounce = value); }
    }

    [TypeConverter(typeof(ExpandableObjectConverter))]
    internal sealed class PlayerDeathGroup : PlayerGroup
    {
        public PlayerDeathGroup(int id) : base(id) { }
        [DisplayName("Death Time (s)")] public float DeathTime { get => Get().deathTime; set => Edit((ref GhostPlayerParams p) => p.deathTime = value); }
        [DisplayName("Death Hop")] public float DeathHop { get => Get().deathHop; set => Edit((ref GhostPlayerParams p) => p.deathHop = value); }
        [DisplayName("Pit Death Pop (fraction of screen)")] public float DeathPop { get => Get().deathPop; set => Edit((ref GhostPlayerParams p) => p.deathPop = value); }
    }

    [TypeConverter(typeof(ExpandableObjectConverter))]
    internal sealed class PlayerModelGroup : PlayerGroup
    {
        public PlayerModelGroup(int id) : base(id) { }
        [DisplayName("Yaw Offset (deg)")] public float YawOffset { get => Get().modelYawOffset; set => Edit((ref GhostPlayerParams p) => p.modelYawOffset = value); }
        [DisplayName("Turn Speed (deg/s)")] public float TurnSpeed { get => Get().modelTurnSpeed; set => Edit((ref GhostPlayerParams p) => p.modelTurnSpeed = value); }
        [DisplayName("Stride Speed (0 = off)")] public float StrideSpeed { get => Get().modelStrideSpeed; set => Edit((ref GhostPlayerParams p) => p.modelStrideSpeed = value); }
        [DisplayName("Attack Time (s)")] public float AttackTime { get => Get().modelAttackTime; set => Edit((ref GhostPlayerParams p) => p.modelAttackTime = value); }
        [DisplayName("Attack Hit At (0-1 of clip)")] public float AttackHitAt { get => Get().modelAttackHitAt; set => Edit((ref GhostPlayerParams p) => p.modelAttackHitAt = value); }
        [DisplayName("Attack Reach")] public float AttackReach { get => Get().modelAttackReach; set => Edit((ref GhostPlayerParams p) => p.modelAttackReach = value); }
        [DisplayName("Melee Damage")] public bool MeleeDamage { get => Get().modelMeleeDamage != 0; set => Edit((ref GhostPlayerParams p) => p.modelMeleeDamage = value ? 1 : 0); }
    }

    // ---------------- Enemy ----------------
    internal abstract class EnemyGroup
    {
        private readonly int _id;
        protected delegate void Mut(ref GhostEnemyParams p);
        protected EnemyGroup(int id) { _id = id; }
        protected GhostEnemyParams Get() { var p = new GhostEnemyParams(); GhostEngine.GhostEngine_GetEnemyParams(_id, ref p); return p; }
        protected void Edit(Mut m) { var p = Get(); m(ref p); GhostEngine.GhostEngine_SetEnemyParams(_id, ref p); }
        public override string ToString() => "";
    }

    [TypeConverter(typeof(ExpandableObjectConverter))]
    internal sealed class EnemyPhysicsGroup : EnemyGroup
    {
        public EnemyPhysicsGroup(int id) : base(id) { }
        [DisplayName("Gravity")] public float Gravity { get => Get().gravity; set => Edit((ref GhostEnemyParams p) => p.gravity = value); }
        [DisplayName("Max Fall Speed")] public float MaxFall { get => Get().maxFall; set => Edit((ref GhostEnemyParams p) => p.maxFall = value); }
        [DisplayName("Fall Limit (Y: below this = gone)")] public float FallLimit { get => Get().fallLimit; set => Edit((ref GhostEnemyParams p) => p.fallLimit = value); }
        [DisplayName("Default Patrol (when Move Offset is 0)")] public float PatrolDefault { get => Get().patrolDefault; set => Edit((ref GhostEnemyParams p) => p.patrolDefault = value); }
        [DisplayName("Ledge Check: Lead (units)")] public float LedgeLead { get => Get().ledgeLead; set => Edit((ref GhostEnemyParams p) => p.ledgeLead = value); }
        [DisplayName("Ledge Check: Depth (units)")] public float LedgeDepth { get => Get().ledgeDepth; set => Edit((ref GhostEnemyParams p) => p.ledgeDepth = value); }
    }

    [TypeConverter(typeof(ExpandableObjectConverter))]
    internal sealed class EnemyAttackGroup : EnemyGroup
    {
        public EnemyAttackGroup(int id) : base(id) { }
        [DisplayName("Attack Range (3D)")] public float AttackRange { get => Get().attackRange; set => Edit((ref GhostEnemyParams p) => p.attackRange = value); }
        [DisplayName("Attack Height Tolerance (3D)")] public float AttackHeight { get => Get().attackHeight; set => Edit((ref GhostEnemyParams p) => p.attackHeight = value); }
        [DisplayName("Attack Time (s, 3D)")] public float AttackTime { get => Get().attackTime; set => Edit((ref GhostEnemyParams p) => p.attackTime = value); }
        [DisplayName("Attack Cooldown (s, 3D)")] public float AttackCooldown { get => Get().attackCooldown; set => Edit((ref GhostEnemyParams p) => p.attackCooldown = value); }
    }

    [TypeConverter(typeof(ExpandableObjectConverter))]
    internal sealed class EnemyDeathGroup : EnemyGroup
    {
        public EnemyDeathGroup(int id) : base(id) { }
        [DisplayName("3D Die Time (s)")] public float ModelDieTime { get => Get().modelDieTime; set => Edit((ref GhostEnemyParams p) => p.modelDieTime = value); }
        [DisplayName("Sprite Die Time (s)")] public float SpriteDieTime { get => Get().spriteDieTime; set => Edit((ref GhostEnemyParams p) => p.spriteDieTime = value); }
        [DisplayName("Flatten Time (s)")] public float FlattenTime { get => Get().flattenTime; set => Edit((ref GhostEnemyParams p) => p.flattenTime = value); }
        [DisplayName("Flatten Height (x)")] public float FlattenScaleY { get => Get().flattenScaleY; set => Edit((ref GhostEnemyParams p) => p.flattenScaleY = value); }
        [DisplayName("Flatten Drop (fraction)")] public float FlattenDrop { get => Get().flattenDrop; set => Edit((ref GhostEnemyParams p) => p.flattenDrop = value); }
        [DisplayName("Bounce-Off Speed")] public float DeathBounce { get => Get().deathBounce; set => Edit((ref GhostEnemyParams p) => p.deathBounce = value); }
        [DisplayName("Shell Grace Time (s)")] public float ShellGrace { get => Get().shellGrace; set => Edit((ref GhostEnemyParams p) => p.shellGrace = value); }
    }

    [TypeConverter(typeof(ExpandableObjectConverter))]
    internal sealed class EnemyModelGroup : EnemyGroup
    {
        public EnemyModelGroup(int id) : base(id) { }
        [DisplayName("Yaw Offset (deg)")] public float YawOffset { get => Get().modelYawOffset; set => Edit((ref GhostEnemyParams p) => p.modelYawOffset = value); }
        [DisplayName("Turn Speed (deg/s)")] public float TurnSpeed { get => Get().modelTurnSpeed; set => Edit((ref GhostEnemyParams p) => p.modelTurnSpeed = value); }
    }

    internal sealed class PathActionConverter : FixedListConverter
    { public static readonly string[] Names = { "Walk", "Sprint", "Jump", "Sprint Jump", "Idle (wait here, then walk)" }; protected override string[] Items => Names; }
    internal sealed class PathLoopConverter : FixedListConverter
    { public static readonly string[] Names = { "Loop (travel back to the first point)", "Stop at the end (idle)", "Ping-Pong (walk it backwards)", "Loop (teleport to the first point)" }; protected override string[] Items => Names; }

    // ---------------- Enemy: pathway following ----------------
    [TypeConverter(typeof(ExpandableObjectConverter))]
    internal sealed class EnemyPathGroup : EnemyGroup
    {
        public EnemyPathGroup(int id) : base(id) { }
        [DisplayName("Follow Pathway (markers tagged Pathway)")] public bool Follow { get => Get().followPathway != 0; set => Edit((ref GhostEnemyParams p) => p.followPathway = value ? 1 : 0); }
        [DisplayName("Pathway Group")] public int Group { get => Get().pathGroup; set => Edit((ref GhostEnemyParams p) => p.pathGroup = value); }
        [DisplayName("End Of Pathway"), TypeConverter(typeof(PathLoopConverter))]
        public string EndOfPath
        {
            get => PathLoopConverter.Names[Math.Clamp(Get().pathLoop, 0, 3)];
            set { int i = Array.IndexOf(PathLoopConverter.Names, value); if (i >= 0) Edit((ref GhostEnemyParams p) => p.pathLoop = i); }
        }
        [DisplayName("Sprint Speed (x Move Speed)")] public float SprintMul { get => Get().pathSprintMul; set => Edit((ref GhostEnemyParams p) => p.pathSprintMul = value); }
        [DisplayName("Jump Height")] public float JumpHeight { get => Get().pathJumpHeight; set => Edit((ref GhostEnemyParams p) => p.pathJumpHeight = value); }
        [DisplayName("Sprint Jump Height")] public float SprintJumpHeight { get => Get().pathSprintJumpHeight; set => Edit((ref GhostEnemyParams p) => p.pathSprintJumpHeight = value); }
    }

    // ---------------- Boss ----------------
    internal abstract class BossGroup
    {
        private readonly int _id;
        protected delegate void Mut(ref GhostBossParams p);
        protected BossGroup(int id) { _id = id; }
        protected GhostBossParams Get() { var p = new GhostBossParams(); GhostEngine.GhostEngine_GetBossParams(_id, ref p); return p; }
        protected void Edit(Mut m) { var p = Get(); m(ref p); GhostEngine.GhostEngine_SetBossParams(_id, ref p); }
        public override string ToString() => "";
    }

    [TypeConverter(typeof(ExpandableObjectConverter))]
    internal sealed class BossMotionGroup : BossGroup
    {
        public BossMotionGroup(int id) : base(id) { }
        [DisplayName("Auto Jump Y (Move Offset Y = hop height)")] public bool AutoJumpY { get => Get().autoJumpY != 0; set => Edit((ref GhostBossParams p) => p.autoJumpY = value ? 1 : 0); }
        [DisplayName("Hop Interval (s)")] public float HopInterval { get => Get().hopInterval; set => Edit((ref GhostBossParams p) => p.hopInterval = value); }
        [DisplayName("Gravity")] public float Gravity { get => Get().gravity; set => Edit((ref GhostBossParams p) => p.gravity = value); }
        [DisplayName("Max Fall Speed")] public float MaxFall { get => Get().maxFall; set => Edit((ref GhostBossParams p) => p.maxFall = value); }
    }

    [TypeConverter(typeof(ExpandableObjectConverter))]
    internal sealed class BossPathGroup : BossGroup
    {
        public BossPathGroup(int id) : base(id) { }
        [DisplayName("Follow Pathway (markers tagged Pathway)")] public bool Follow { get => Get().followPathway != 0; set => Edit((ref GhostBossParams p) => p.followPathway = value ? 1 : 0); }
        [DisplayName("Pathway Group")] public int Group { get => Get().pathGroup; set => Edit((ref GhostBossParams p) => p.pathGroup = value); }
        [DisplayName("End Of Pathway"), TypeConverter(typeof(PathLoopConverter))]
        public string EndOfPath
        {
            get => PathLoopConverter.Names[Math.Clamp(Get().pathLoop, 0, 3)];
            set { int i = Array.IndexOf(PathLoopConverter.Names, value); if (i >= 0) Edit((ref GhostBossParams p) => p.pathLoop = i); }
        }
        [DisplayName("Sprint Speed (x Move Speed)")] public float SprintMul { get => Get().pathSprintMul; set => Edit((ref GhostBossParams p) => p.pathSprintMul = value); }
        [DisplayName("Jump Height")] public float JumpHeight { get => Get().pathJumpHeight; set => Edit((ref GhostBossParams p) => p.pathJumpHeight = value); }
        [DisplayName("Sprint Jump Height")] public float SprintJumpHeight { get => Get().pathSprintJumpHeight; set => Edit((ref GhostBossParams p) => p.pathSprintJumpHeight = value); }
    }

    [TypeConverter(typeof(ExpandableObjectConverter))]
    internal sealed class BossModelGroup : BossGroup
    {
        public BossModelGroup(int id) : base(id) { }
        [DisplayName("Yaw Offset (deg)")] public float YawOffset { get => Get().modelYawOffset; set => Edit((ref GhostBossParams p) => p.modelYawOffset = value); }
        [DisplayName("Turn Speed (deg/s)")] public float TurnSpeed { get => Get().modelTurnSpeed; set => Edit((ref GhostBossParams p) => p.modelTurnSpeed = value); }
    }

    // ---------------- Pathway marker (Role = Custom, Custom Tag = Pathway) ----------------
    [TypeConverter(typeof(ExpandableObjectConverter))]
    internal sealed class PathPointGroup
    {
        private readonly int _id;
        private delegate void Mut(ref GhostPathParams p);
        public PathPointGroup(int id) { _id = id; }
        private GhostPathParams Get() { var p = new GhostPathParams(); GhostEngine.GhostEngine_GetPathParams(_id, ref p); return p; }
        private void Edit(Mut m) { var p = Get(); m(ref p); GhostEngine.GhostEngine_SetPathParams(_id, ref p); }
        public override string ToString() => "";

        [DisplayName("Pathway Group")] public int Group { get => Get().pathGroup; set => Edit((ref GhostPathParams p) => p.pathGroup = value); }
        [DisplayName("Action (travel to the next marker)"), TypeConverter(typeof(PathActionConverter))]
        public string Action
        {
            get => PathActionConverter.Names[Math.Clamp(Get().action, 0, 4)];
            set { int i = Array.IndexOf(PathActionConverter.Names, value); if (i >= 0) Edit((ref GhostPathParams p) => p.action = i); }
        }
        [DisplayName("Hold Time (s, idle on arrival)")] public float HoldTime { get => Get().holdTime; set => Edit((ref GhostPathParams p) => p.holdTime = value); }
        [DisplayName("Jump Height (0 = follower default)")] public float JumpHeight { get => Get().jumpHeight; set => Edit((ref GhostPathParams p) => p.jumpHeight = value); }
    }
}