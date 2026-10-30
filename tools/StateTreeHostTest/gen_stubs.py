"""Generates out/stub_tasks.cpp: a host stand-in per task class of ../StateTreeEditor/schema.json (its properties and
delegates, random results and values) for the state tree host test."""
import glob
import json
import os
import re
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
schema = json.load(open(os.path.join(REPO, "tools", "StateTreeEditor", "schema.json")))

REAL = {"Task_Wait", "Task_RunSubtree", "STAI_GlobalTask", "STAI_FinishTreeTask", "STAI_SendEventTask"}

TYPES = {
    "bool": "bool", "float": "float", "int32": "int32", "uint32": "uint32", "std::string": "std::string",
    "std::map<uint32,bool>": "std::map<uint32, bool>", "std::map<uint32,uint32>": "std::map<uint32, uint32>",
    "ObjectGuid": "HostGuid", "STAI_Location": "HostLocation", "STAI_Position": "HostPosition",
    "STAI_SpellCauses": "HostVec<1>", "std::vector<BuffNeed>": "HostVec<2>", "std::vector<ObjectGuid>": "HostVec<3>",
    "std::vector<std::shared_ptr<STAI_ItemRoll>>": "HostVec<4>", "std::vector<uint32>": "HostVec<5>",
}

# values the trees compare text properties with / keys they read from maps, per Class::member
texts, keys = {}, {}
for f in glob.glob(os.path.join(REPO, "statetree", "**", "STAI_*.xml"), recursive=True):
    for c in ET.parse(f).getroot().iter("Condition"):
        b = c.get("binding", "")
        m = re.search(r"\|([A-Za-z_]+)(?:\[[^\]]*\])?::(\w+)$", b)
        if not m:
            continue
        member = m.group(1) + "::" + m.group(2)
        if c.get("key"):
            keys.setdefault(member, set()).add(c.get("key"))
        if c.get("compare") == "equal" and c.get("value") and not re.match(r"^-?[0-9.]+$", c.get("value")):
            texts.setdefault(member, set()).add(c.get("value"))

out = ['#include "harness.h"', "", "using namespace ai;", "", "namespace", "{"]
registrations = []
for cls, info in sorted(schema["tasks"].items()):
    if cls in REAL:
        continue
    ctx = "Ctx_" + cls
    members = []
    randomize = []
    for i, p in enumerate(info["properties"]):
        t = TYPES[p["type"]]
        name = p["name"]
        members.append("        %s %s{};" % (t, name))
        key = cls + "::" + name
        if t == "bool":
            randomize.append("            c.%s = HostChance(30);" % name)
        elif t in ("float", "int32", "uint32"):
            randomize.append("            c.%s = %s(urand(0, 100));" % (name, t))
        elif t == "std::string":
            values = sorted(texts.get(key, {"a"})) + [""]
            randomize.append("            { static const char* values[] = { %s }; c.%s = values[urand(0, %d)]; }" % (
                ", ".join('"%s"' % v for v in values), name, len(values) - 1))
        elif t.startswith("std::map"):
            ks = sorted(keys.get(key, {"1"}))
            randomize.append("            c.%s.clear(); %s" % (name, " ".join("if (HostChance(50)) c.%s[%s] = 1;" % (name, k) for k in ks)))
        elif t.startswith("HostVec"):
            randomize.append("            c.%s.assign(urand(0, 1) * urand(0, 2), 1);" % name)
        else:
            randomize.append("            c.%s.value = HostChance(50);" % name)
    delegates = info.get("delegates", [])
    for d in delegates:
        members.append("        FSTAI_Delegate %s;" % d)
    out.append("    struct %s : public STAI_TaskContext" % ctx)
    out.append("    {")
    out.extend(members)
    out.append("    };")
    out.append("")
    base = "STAI_DataProviderBase" if info.get("global") else "STAI_TaskBase"
    out.append("    class Stub_%s : public %s" % (cls, base))
    out.append("    {")
    out.append("    public:")
    out.append("        static void Validate(const STAI_TaskConfig& config) {}")
    out.append("        static std::unique_ptr<STAI_TaskBase> Create(const STAI_TaskConfig& config) { return std::make_unique<Stub_%s>(); }" % cls)
    out.append('        virtual const char* GetNodeClass() const override { return "%s"; }' % cls)
    out.append('        virtual std::string GetName() const override { return "%s"; }' % cls)
    out.append("        virtual const STAI_TaskContextType& GetContextType() const override { return STAI_TaskContextType::Of<%s>(); }" % ctx)
    out.append("        virtual bool ShouldCallTick() const override { return true; }")
    out.append("        virtual const std::vector<STAI_PropertyDef>& GetProperties() const override")
    out.append("        {")
    out.append("            static const std::vector<STAI_PropertyDef> properties = { %s };" % ", ".join(
        'STAI_PropertyDef::Of("%s", &%s::%s)' % (p["name"], ctx, p["name"]) for p in info["properties"]))
    out.append("            return properties;")
    out.append("        }")
    out.append("        virtual const std::vector<STAI_DelegateDef>& GetDelegates() const override")
    out.append("        {")
    out.append("            static const std::vector<STAI_DelegateDef> delegates = { %s };" % ", ".join(
        'STAI_DelegateDef::Of("%s", &%s::%s)' % (d, ctx, d) for d in delegates))
    out.append("            return delegates;")
    out.append("        }")
    out.append("    protected:")
    if info.get("global"):
        out.append("        virtual EActionRunStatus OnTick(STAI_TaskContext& base, uint32 elapsed, bool minimal) const override")
        out.append("        {")
        out.append("            %s& c = ContextAs<%s>(base);" % (ctx, ctx))
        out.append("            if (!HostChance(20))")
        out.append("                return EActionRunStatus::Running;")
        out.extend(randomize)
        for d in delegates:
            out.append("            if (HostChance(15)) c.%s.Broadcast();" % d)
        out.append("            return EActionRunStatus::Running;")
        out.append("        }")
    else:
        out.append("        virtual EActionRunStatus OnEnterState(STAI_TaskContext& c) const override { ++hostStats.taskEnters; return HostEnterResult(); }")
        out.append("        virtual EActionRunStatus OnTick(STAI_TaskContext& c, uint32 elapsed, bool minimal) const override { return HostTickResult(); }")
        out.append("        virtual void OnExitState(STAI_TaskContext& c) const override { ++hostStats.taskExits; }")
    out.append("    };")
    out.append("")
    registrations.append('        factory.RegisterTask("%s", { &Stub_%s::Validate, &Stub_%s::Create });' % (cls, cls, cls))

out.append("}")
out.append("")
out.append("void RegisterStubTasks(STAI_NodeFactory& factory)")
out.append("{")
out.extend(registrations)
out.append("}")
os.makedirs(os.path.join(HERE, "out"), exist_ok=True)
open(os.path.join(HERE, "out", "stub_tasks.cpp"), "w").write("\n".join(out) + "\n")
print("stubs:", len(registrations))
