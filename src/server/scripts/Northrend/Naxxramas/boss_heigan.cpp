/*
 * Copyright (C) 2008-2012 TrinityCore <http://www.trinitycore.org/>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "ScriptPCH.h"
#include "naxxramas.h"

enum ScriptTexts
{
    SAY_AGGRO           = 0,
    SAY_SLAY            = 1,
    SAY_PHASE           = 2,
    EMOTE_TELEPORT1     = 3,
    EMOTE_TELEPORT2     = 4,
    SAY_DEATH           = 5,
};

enum Spells
{
    SPELL_SPELL_DISRUPTION      = 29310,
    SPELL_DECREPIT_FEVER_10     = 29998,
    SPELL_DECREPIT_FEVER_25     = 55011,
    SPELL_PLAGUE_CLOUD          = 29350,
};

enum Events
{
    EVENT_NONE = 1,
    EVENT_DISRUPT,
    EVENT_FEVER,
    EVENT_ERUPT,
    EVENT_PHASE1,
    EVENT_PHASE2,
};

enum Phases
{
    PHASE_1,
    PHASE_2,
};

#define ACTION_SAFETY_DANCE_FAIL 1
#define DATA_SAFETY_DANCE        19962139

class boss_heigan : public CreatureScript
{
public:
    boss_heigan() : CreatureScript("boss_heigan") { }

    CreatureAI* GetAI(Creature* creature) const
    {
        return new boss_heiganAI (creature);
    }

    struct boss_heiganAI : public BossAI
    {
        boss_heiganAI(Creature* creature) : BossAI(creature, BOSS_HEIGAN) {}

        uint32 eruptSection;
        bool eruptDirection;
        bool safetyDance;
        Phases phase;

        void KilledUnit(Unit* who)
        {
            Talk(SAY_SLAY);
            if (who->GetTypeId() == TYPEID_PLAYER)
                safetyDance = false;
        }

        void SetData(uint32 id, uint32 data)
        {
            if (id == DATA_SAFETY_DANCE)
                safetyDance = data ? true : false;
        }

        uint32 GetData(uint32 type)
        {
            if (type == DATA_SAFETY_DANCE)
                return safetyDance ? 1 : 0;

            return 0;
        }

        void JustDied(Unit* /*Killer*/)
        {
            _JustDied();
            Talk(SAY_DEATH);
        }

        void EnterCombat(Unit* /*who*/)
        {
            _EnterCombat();
            Talk(SAY_AGGRO);
            EnterPhaseFight();
            safetyDance = true;
        }
		
        void EnterPhaseFight()
        {
            events.Reset();
            eruptSection = 3;
            events.ScheduleEvent(EVENT_DISRUPT, urand(10000, 25000));
            events.ScheduleEvent(EVENT_FEVER, urand(15000, 20000));
            events.ScheduleEvent(EVENT_PHASE2, 90000);
            events.ScheduleEvent(EVENT_ERUPT, 15000);
            phase = PHASE_1;
        }

        void UpdateAI(const uint32 diff)
        {
            if (!UpdateVictim() || !CheckInRoom())
                return;

            events.Update(diff);

            while (uint32 eventId = events.ExecuteEvent())
            {
                switch (eventId)
                {
                    case EVENT_DISRUPT:
                        DoCastAOE(SPELL_SPELL_DISRUPTION);
                        events.ScheduleEvent(EVENT_DISRUPT, urand(5000, 10000));
                        break;
                    case EVENT_FEVER:
                        DoCastAOE(RAID_MODE(SPELL_DECREPIT_FEVER_10, SPELL_DECREPIT_FEVER_25));
                        events.ScheduleEvent(EVENT_FEVER, urand(20000, 25000));
                        break;
                    case EVENT_PHASE1:
                        Talk(EMOTE_TELEPORT2);
                        me->SetReactState(REACT_AGGRESSIVE);
                        me->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE);
                        DoZoneInCombat();
                        events.Reset();
                        eruptSection = 3;
                        events.ScheduleEvent(EVENT_DISRUPT, urand(10000, 25000));
                        events.ScheduleEvent(EVENT_FEVER, urand(15000, 20000));
                        events.ScheduleEvent(EVENT_PHASE2, 90000);
                        events.ScheduleEvent(EVENT_ERUPT, 15000);
                        phase = PHASE_1;
                        break;
                    case EVENT_PHASE2:
                        Talk(SAY_PHASE);
                        Talk(EMOTE_TELEPORT1);
                        me->SetReactState(REACT_PASSIVE);
                        me->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE);
                        me->AttackStop();
                        me->RemoveAllAuras();
                        float x, y, z, o;
                        me->GetHomePosition(x, y, z, o);
                        me->NearTeleportTo(x, y, z, o);
                        DoCastAOE(SPELL_PLAGUE_CLOUD);
                        events.Reset();
                        events.ScheduleEvent(EVENT_PHASE1, 45000);
                        events.ScheduleEvent(EVENT_ERUPT, 8000);
                        phase = PHASE_2;
                        break;
                    case EVENT_ERUPT:
                        instance->SetData(DATA_HEIGAN_ERUPT, eruptSection);
                        TeleportCheaters();

                        if (eruptSection == 0)
                            eruptDirection = true;
                        else if (eruptSection == 3)
                            eruptDirection = false;

                        eruptDirection ? ++eruptSection : --eruptSection;

                        events.ScheduleEvent(EVENT_ERUPT, phase == PHASE_1 ? 10000 : 3000);
                        break;
                }
            }

            DoMeleeAttackIfReady();
        }
    };

};

class spell_heigan_eruption : public SpellScriptLoader
{
    public:
        spell_heigan_eruption() : SpellScriptLoader("spell_heigan_eruption") { }

        class spell_heigan_eruption_SpellScript : public SpellScript
        {
            PrepareSpellScript(spell_heigan_eruption_SpellScript);

            void HandleScript(SpellEffIndex /*eff*/)
            {
                Unit* caster = GetCaster();
                if (!caster || !GetHitPlayer())
                    return;

                if (GetHitDamage() >= int32(GetHitPlayer()->GetHealth()))
                    if (InstanceScript* instance = caster->GetInstanceScript())
                        if (Creature* Heigan = ObjectAccessor::GetCreature(*caster, instance->GetData64(BOSS_HEIGAN)))
                            Heigan->AI()->SetData(DATA_SAFETY_DANCE, 0);
            }

            void Register()
            {
                OnEffectHitTarget += SpellEffectFn(spell_heigan_eruption_SpellScript::HandleScript, EFFECT_0, SPELL_EFFECT_SCHOOL_DAMAGE);
            }
        };

        SpellScript* GetSpellScript() const
        {
            return new spell_heigan_eruption_SpellScript();
        }
};

class achievement_safety_dance : public AchievementCriteriaScript
{
    public:
        achievement_safety_dance() : AchievementCriteriaScript("achievement_safety_dance")
        {
        }

        bool OnCheck(Player* /*player*/, Unit* target)
        {
            if (!target)
                return false;

            if (Creature* Heigan = target->ToCreature())
                if (Heigan->AI()->GetData(DATA_SAFETY_DANCE))
                    return true;

            return false;
        }
};

void AddSC_boss_heigan()
{
    new boss_heigan();
    new spell_heigan_eruption();
    new achievement_safety_dance();
}
