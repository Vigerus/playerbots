'use strict';
(function () {

// ---------- constants

const INDENT = '  ';
const STATE_TYPES = ['State', 'Subtree', 'Linked'];
const SELECTIONS = ['TrySelectChildrenInOrder', 'TrySelectChildrenAtRandom', 'TrySelectChildrenWithHighestUtility',
  'TrySelectChildrenAtRandomWeightedByUtility', 'TryFollowTransitions'];
const UTILITY_SELECTIONS = ['TrySelectChildrenWithHighestUtility', 'TrySelectChildrenAtRandomWeightedByUtility'];
const SELECTION_ICONS = {
  TrySelectChildrenInOrder: '↓', TrySelectChildrenAtRandom: '⚄', TrySelectChildrenWithHighestUtility: '▲',
  TrySelectChildrenAtRandomWeightedByUtility: '⚖', TryFollowTransitions: '↪',
};
const COMPLETIONS = ['AlwaysFinish', 'FinishWhenSucceeded', 'FinishWhenFailed', 'IgnoreResult'];
const TRIGGERS = ['OnStateCompleted', 'OnStateSucceeded', 'OnStateFailed', 'OnTick', 'OnEvent', 'OnDelegate'];
const PRIORITIES = ['Low', 'Normal', 'Medium', 'High', 'Critical'];
const COMPARES = ['equal', 'lesser', 'lesser or equal', 'greater', 'greater or equal'];
const COMPARE_OPS = { equal: '==', lesser: '<', 'lesser or equal': '<=', greater: '>', 'greater or equal': '>=' };
const SPECIAL_TARGETS = ['Root', 'NextState', 'Succeeded', 'Failed'];
const BOOLS = ['true', 'false'];
const TREE_ORDER = ['Comment', 'GlobalTasks', 'State'];
const STATE_ORDER = ['Comment', 'EnterConditions', 'Considerations', 'Tasks', 'Transitions', 'State'];
const TRANSITION_ORDER = ['Comment', 'Conditions'];
const TASK_STRUCTURE = ['Comment', 'Binding', 'Conditions'];
const CONDITION_TAGS = ['Condition', 'Any', 'All'];
const CONTAINERS = ['Tasks', 'Transitions', 'EnterConditions', 'Considerations', 'Conditions', 'GlobalTasks'];
const PATH_ATTRS = ['target', 'source', 'external', 'binding'];
const ATTR_ORDER = {
  State: ['name', 'type', 'linkedSubtree', 'selection', 'weight', 'retryDelay', 'enabled', '*'],
  Task: ['class', 'name', '*', 'completion', 'show_log', 'enabled'],
  Transition: ['trigger', 'source', 'event', 'target', 'priority', 'delay', 'enabled', '*'],
  Condition: ['class', '*'],
  Consideration: ['class', '*'],
  Binding: ['local', 'external', '*'],
};
const KNOWN_ATTRS = {
  State: ['name', 'type', 'linkedSubtree', 'selection', 'weight', 'retryDelay', 'enabled'],
  Task: ['class', 'name', 'completion', 'show_log', 'enabled'],
  Transition: ['trigger', 'source', 'event', 'target', 'priority', 'delay', 'enabled'],
};
const TEMPLATE = '<?xml version="1.0" encoding="utf-8"?>\n<StateTree version="2">\n  <Comment>new state tree</Comment>\n' +
  '  <State name="Idle">\n    <Tasks>\n      <Task class="Task_Wait">\n        <Delay value="0"/>\n      </Task>\n' +
  '    </Tasks>\n  </State>\n</StateTree>\n';

// ---------- app state

let schema = { tasks: {}, conditions: {}, considerations: {}, typeAliases: {} };
let files = [];
const sessions = new Map();
let cur = null;     // the open session: {path, mtime, savedText, text, history, index, doc, parseError, collapsed, sel}
let D = null;       // the open XMLDocument
let sel = null;     // the selected element of D
let problems = [];
let indexCache = null;
let elRows = new Map();
let rowOrder = [];
let drag = null;
let findText = '';
let sourceOpen = false;
let sourceDirty = false;
let validateTimer = 0;
let uid = 0;
const collapsedFolders = new Set();
const collapsedGroups = new Set(storageGet('ste.groups', '').split('|').filter(Boolean));

const $ = (q) => document.querySelector(q);

// ---------- small helpers

function storageGet(key, fallback) {
  try { const v = localStorage.getItem(key); return v === null ? fallback : v; } catch (e) { return fallback; }
}
function storageSet(key, value) {
  try { localStorage.setItem(key, value); } catch (e) { /* private mode */ }
}

function h(tag, props, ...children) {
  const el = document.createElement(tag);
  if (props) {
    for (const k of Object.keys(props)) {
      const v = props[k];
      if (v === null || v === undefined || v === false) continue;
      if (k === 'class') el.className = v;
      else if (k === 'style') el.style.cssText = v;
      else if (k === 'dataset') Object.assign(el.dataset, v);
      else if (k.startsWith('on') && typeof v === 'function') el.addEventListener(k.slice(2), v);
      else if (k === 'value' || k === 'checked' || k === 'disabled' || k === 'selected' || k === 'title' || k === 'draggable') el[k] = v;
      else el.setAttribute(k, v === true ? '' : v);
    }
  }
  for (const c of children.flat(Infinity)) {
    if (c === null || c === undefined || c === false) continue;
    el.append(c.nodeType ? c : String(c));
  }
  return el;
}

function toast(message, kind) {
  const t = $('#toast');
  t.textContent = message;
  t.className = 'toast' + (kind ? ' ' + kind : '');
  clearTimeout(toast.timer);
  toast.timer = setTimeout(() => t.classList.add('hidden'), kind === 'error' ? 6000 : 2500);
}

function isNum(v) { return typeof v === 'string' && v.trim() !== '' && isFinite(Number(v)); }
function isUInt(v) { return /^\d+$/.test(v || ''); }
function plural(n, word) { return n + ' ' + word + (n === 1 ? '' : 's'); }

function docHtml(text) {
  const out = h('div', { class: 'doc' });
  String(text || '').split('`').forEach((part, i) => out.append(i % 2 ? h('code', null, part) : part));
  return out;
}

// ---------- XML model helpers

function kids(el, tag) {
  const out = [];
  if (!el) return out;
  for (let c = el.firstElementChild; c; c = c.nextElementSibling) {
    if (!tag || (Array.isArray(tag) ? tag.includes(c.tagName) : c.tagName === tag)) out.push(c);
  }
  return out;
}
function kid(el, tag) {
  if (!el) return null;
  for (let c = el.firstElementChild; c; c = c.nextElementSibling) if (c.tagName === tag) return c;
  return null;
}
function attr(el, name, def) {
  return el && el.hasAttribute(name) ? el.getAttribute(name) : (def === undefined ? '' : def);
}
function has(el, name) { return !!el && el.hasAttribute(name); }

function setAttr(el, name, value) {
  if (value === null || value === undefined) { el.removeAttribute(name); return; }
  value = String(value);
  if (el.hasAttribute(name)) { el.setAttribute(name, value); return; }
  const order = ATTR_ORDER[el.tagName];
  if (!order) { el.setAttribute(name, value); return; }
  const rank = (n) => { let i = order.indexOf(n); if (i < 0) i = order.indexOf('*'); return i < 0 ? order.length : i; };
  const existing = Array.from(el.attributes, (a) => [a.name, a.value]);
  const r = rank(name);
  const pos = existing.findIndex(([n]) => rank(n) > r);
  if (pos < 0) { el.setAttribute(name, value); return; }
  existing.splice(pos, 0, [name, value]);
  for (const [n] of existing) el.removeAttribute(n);
  for (const [n, v] of existing) el.setAttribute(n, v);
}
function setOrRemove(el, name, value) {
  if (value === '' || value === null || value === undefined) el.removeAttribute(name);
  else setAttr(el, name, value);
}

function mk(tag, attrs) {
  const el = D.createElement(tag);
  for (const k of Object.keys(attrs || {})) if (attrs[k] !== null && attrs[k] !== undefined) el.setAttribute(k, attrs[k]);
  return el;
}
function insertOrdered(parent, el, order) {
  const r = order.indexOf(el.tagName);
  for (let c = parent.firstElementChild; c; c = c.nextElementSibling) {
    if (order.indexOf(c.tagName) > r) { parent.insertBefore(el, c); return el; }
  }
  parent.appendChild(el);
  return el;
}
function orderOf(parent) {
  if (parent.tagName === 'StateTree') return TREE_ORDER;
  if (parent.tagName === 'State') return STATE_ORDER;
  if (parent.tagName === 'Transition') return TRANSITION_ORDER;
  return null;
}
function ensureContainer(parent, tag) {
  let c = kid(parent, tag);
  if (c) return c;
  c = mk(tag);
  const order = orderOf(parent);
  if (order) insertOrdered(parent, c, order);
  else parent.appendChild(c);
  return c;
}
function removeIfEmpty(container) {
  if (!container || !CONTAINERS.includes(container.tagName) || container.attributes.length) return;
  for (let c = container.firstChild; c; c = c.nextSibling) if (c.nodeType === 1 || c.nodeType === 8) return;
  container.remove();
}
function cleanupContainers(scope) {
  for (const el of Array.from((scope || D.documentElement).getElementsByTagName('*'))) {
    if (CONTAINERS.includes(el.tagName) && el.parentNode) removeIfEmpty(el);
  }
}

function getComment(el) {
  const c = kid(el, 'Comment');
  return c ? c.textContent.trim().split('\n').map((s) => s.trim()).join('\n') : '';
}
function setComment(el, text) {
  let c = kid(el, 'Comment');
  const t = String(text || '').replace(/\r/g, '').trim();
  if (!t) { if (c) c.remove(); return; }
  if (!c) { c = mk('Comment'); el.insertBefore(c, el.firstChild); }
  c.textContent = t.split('\n').map((s) => s.trim()).join('\n');
}

// ---------- serializer (matches convert_v1_to_v2.py)

function escText(s) { return s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;'); }
function quoteAttr(s) {
  s = escText(s).replace(/\n/g, '&#10;').replace(/\r/g, '&#13;').replace(/\t/g, '&#9;');
  if (s.includes('"')) return s.includes("'") ? '"' + s.replace(/"/g, '&quot;') + '"' : "'" + s + "'";
  return '"' + s + '"';
}
function writeNode(node, depth, out) {
  const pad = INDENT.repeat(depth);
  if (node.nodeType === 8) { out.push(pad + '<!--' + node.data + '-->'); return; }
  if (node.nodeType !== 1) return;
  const tag = node.tagName;
  let attrs = '';
  for (const a of node.attributes) attrs += ' ' + a.name + '=' + quoteAttr(a.value);
  const children = [];
  let text = '';
  for (let c = node.firstChild; c; c = c.nextSibling) {
    if (c.nodeType === 1 || c.nodeType === 8) children.push(c);
    else if ((c.nodeType === 3 || c.nodeType === 4) && !children.length) text += c.data;
  }
  text = text.trim();
  if (tag === 'Comment') {
    let body = '';
    text.split('\n').forEach((line, i) => {
      line = line.trim();
      body += i === 0 ? escText(line) : '\n' + (line ? pad + INDENT + escText(line) : '');
    });
    out.push(pad + '<Comment>' + body + '</Comment>');
    return;
  }
  if (!children.length && !text) { out.push(pad + '<' + tag + attrs + '/>'); return; }
  if (!children.length) { out.push(pad + '<' + tag + attrs + '>' + escText(text) + '</' + tag + '>'); return; }
  out.push(pad + '<' + tag + attrs + '>');
  for (const c of children) writeNode(c, depth + 1, out);
  out.push(pad + '</' + tag + '>');
}
function serialize(doc) {
  const out = ['<?xml version="1.0" encoding="utf-8"?>'];
  for (let n = doc.firstChild; n; n = n.nextSibling) writeNode(n, 0, out);
  return out.join('\n') + '\n';
}
function parseXml(text) {
  const doc = new DOMParser().parseFromString(text, 'application/xml');
  const error = doc.getElementsByTagName('parsererror')[0];
  if (error) {
    const text = error.textContent.replace(/\s+/g, ' ').trim();
    const m = /(error on line \d+ at column \d+:.*?)(?:Below is a rendering|$)/.exec(text);
    return { error: (m ? m[1].trim() : text) || 'XML parse error' };
  }
  return { doc };
}

// ---------- tree queries

function root() { return D ? D.documentElement : null; }
function invalidate() { indexCache = null; }
function index() {
  if (indexCache) return indexCache;
  const states = [];
  const byName = new Map();
  (function walk(parent) {
    for (const s of kids(parent, 'State')) {
      states.push(s);
      const n = attr(s, 'name');
      if (!byName.has(n)) byName.set(n, s);
      walk(s);
    }
  })(root());
  indexCache = { states, byName };
  return indexCache;
}
function allStates() { return index().states; }
function stateByName(name) { return index().byName.get(name) || null; }
function globalTasksEl() { return kid(root(), 'GlobalTasks'); }
function globalTasks() { return kids(globalTasksEl(), 'Task'); }
function tasksOf(state) { return kids(kid(state, 'Tasks'), 'Task'); }
function childStates(el) { return kids(el, 'State'); }
function isGlobalTask(t) { return !!t && t.tagName === 'Task' && t.parentNode && t.parentNode.tagName === 'GlobalTasks'; }
function ownerState(el) {
  for (let n = el; n && n.nodeType === 1; n = n.parentNode) if (n.tagName === 'State') return n;
  return null;
}
function ownerOf(el, tags) {
  for (let n = el ? el.parentNode : null; n && n.nodeType === 1; n = n.parentNode) if (tags.includes(n.tagName)) return n;
  return null;
}
function statePath(state) {
  const names = [];
  for (let s = state; s && s.tagName === 'State'; s = s.parentNode) names.unshift(attr(s, 'name'));
  return 'Root|' + names.join('|');
}
function resolveStateSegs(segs) {
  if (segs.length === 1) return stateByName(segs[0]);
  let p = root();
  for (const n of segs) {
    p = kids(p, 'State').find((s) => attr(s, 'name') === n);
    if (!p) return null;
  }
  return p;
}
function taskDef(cls) { return schema.tasks[cls] || null; }
function propOf(def, name) { return def ? (def.properties || []).find((p) => p.name === name) || null : null; }
function canonType(t) {
  const plain = String(t || '').replace(/\s+/g, '');
  const aliases = schema.typeAliases || {};
  for (const k of Object.keys(aliases)) if (k.replace(/\s+/g, '') === plain) return aliases[k].replace(/\s+/g, '');
  return plain;
}

const MEMBER_RE = /^([A-Za-z_]\w*)(?:\[([^\]]*)\])?::([A-Za-z_]\w*)$/;
function parseMemberPath(p) {
  if (!p) return { error: 'empty path' };
  const segs = p.split('|');
  if (segs[0] !== 'Root' || segs.length < 2) return { error: 'expected Root|<TaskClass>::<member> or Root|<state>|<TaskClass>::<member>' };
  const m = MEMBER_RE.exec(segs[segs.length - 1]);
  if (!m) return { error: 'expected <TaskClass>::<member> or <TaskClass>[<task name>]::<member> at the end' };
  return { states: segs.slice(1, -1), cls: m[1], name: m[2], member: m[3] };
}
function resolveMember(p, want) {
  const r = parseMemberPath(p);
  if (r.error) return r;
  let scope;
  let where;
  if (r.states.length) {
    const st = resolveStateSegs(r.states);
    if (!st) return Object.assign(r, { error: "unknown state '" + r.states.join('|') + "'" });
    r.state = st;
    scope = tasksOf(st);
    where = "state '" + attr(st, 'name') + "'";
  } else {
    scope = globalTasks();
    where = 'the global tasks';
  }
  const task = scope.find((t) => attr(t, 'class') === r.cls && (r.name === undefined || attr(t, 'name') === r.name));
  if (!task) return Object.assign(r, { error: "no task of class '" + r.cls + "'" + (r.name !== undefined ? " named '" + r.name + "'" : '') + ' in ' + where });
  r.task = task;
  const def = taskDef(r.cls);
  if (!def) return Object.assign(r, { error: "unknown task class '" + r.cls + "'" });
  if (want === 'delegate') {
    if (!def.delegates.includes(r.member)) {
      return Object.assign(r, { error: "'" + r.member + "' is not a delegate of " + r.cls + (propOf(def, r.member) ? ' (it is a property)' : '') });
    }
  } else {
    r.prop = propOf(def, r.member);
    if (!r.prop) return Object.assign(r, { error: "'" + r.member + "' is not a property of " + r.cls + (def.delegates.includes(r.member) ? ' (it is a delegate)' : '') });
  }
  return r;
}
function resolveTarget(t) {
  if (SPECIAL_TARGETS.includes(t)) return { special: t };
  const segs = String(t || '').split('|');
  if (segs[0] !== 'Root' || segs.length < 2) return { error: 'target must be Root, NextState, Succeeded, Failed or Root|<state>' };
  const st = resolveStateSegs(segs.slice(1));
  if (!st) return { error: "unknown state '" + segs.slice(1).join('|') + "'" };
  return { state: st };
}

// tasks of a scope by the reference a path uses for them (Cls or Cls[name])
function taskRefs(tasks) {
  const counts = {};
  for (const t of tasks) counts[attr(t, 'class')] = (counts[attr(t, 'class')] || 0) + 1;
  const out = [];
  const firstSeen = {};
  for (const t of tasks) {
    const cls = attr(t, 'class');
    let ref = cls;
    if (counts[cls] > 1) {
      if (has(t, 'name')) ref = cls + '[' + attr(t, 'name') + ']';
      else if (firstSeen[cls]) continue;
    }
    firstSeen[cls] = true;
    out.push({ task: t, ref });
  }
  return out;
}
function memberGroups(want, exclude) {
  const groups = [];
  const add = (label, prefix, tasks) => {
    const items = [];
    for (const { task, ref } of taskRefs(tasks)) {
      if (task === exclude) continue;
      const def = taskDef(attr(task, 'class'));
      if (!def) continue;
      const members = want === 'delegate' ? def.delegates.map((n) => ({ name: n })) : def.properties;
      for (const m of members) {
        items.push({ value: prefix + ref + '::' + m.name, label: ref + '::' + m.name, type: m.type || '', traits: m.traits || [] });
      }
    }
    if (items.length) groups.push({ label, items });
  };
  add('Global tasks', 'Root|', globalTasks());
  for (const s of allStates()) add('State ' + attr(s, 'name'), 'Root|' + attr(s, 'name') + '|', tasksOf(s));
  return groups;
}

// ---------- locators, selection

function locate(el) {
  if (!el || !D) return null;
  const path = [];
  let n = el;
  while (n && n !== D.documentElement) {
    const p = n.parentNode;
    if (!p || p.nodeType !== 1) return null;
    path.unshift(Array.prototype.indexOf.call(p.children, n));
    n = p;
  }
  return n === D.documentElement ? path : null;
}
function resolveLoc(path) {
  if (!path || !D) return null;
  let n = D.documentElement;
  for (const i of path) { n = n.children[i]; if (!n) return null; }
  return n;
}
function normalizeSel(el) {
  if (!el || !D || !D.documentElement.contains(el)) return null;
  for (let n = el; n && n.nodeType === 1; n = n.parentNode) {
    if (n === D.documentElement) return n;
    switch (n.tagName) {
      case 'State': case 'Task': case 'Transition': case 'Condition': case 'Any': case 'All': case 'Consideration':
        return n;
    }
  }
  return D.documentElement;
}
function kindOf(el) {
  if (!el) return null;
  if (el === root()) return 'tree';
  switch (el.tagName) {
    case 'State': return 'state';
    case 'Task': return isGlobalTask(el) ? 'globalTask' : 'task';
    case 'Transition': return 'transition';
    case 'Condition': return 'condition';
    case 'Any': case 'All': return 'group';
    case 'Consideration': return 'consideration';
  }
  return 'other';
}
function rowOf(el) {
  for (let n = el; n && n.nodeType === 1; n = n.parentNode) {
    if (n === root() || n.tagName === 'State' || isGlobalTask(n)) return n;
  }
  return null;
}
function expandTo(el) {
  if (!cur) return;
  for (let n = el ? el.parentNode : null; n && n.nodeType === 1; n = n.parentNode) {
    if (n.tagName === 'State') cur.collapsed.delete(attr(n, 'name'));
  }
}
function select(el, opts) {
  opts = opts || {};
  sel = el ? normalizeSel(el) : null;
  if (cur) cur.sel = sel;
  expandTo(sel);
  renderOutline();
  renderDetails();
  if (opts.scroll !== false) scrollToSel();
  if (opts.focusName) {
    setTimeout(() => { const i = $('#details [data-focus="name"]'); if (i) { i.focus(); i.select(); } }, 0);
  }
}
function scrollToSel() {
  const target = elRows.get(sel) || elRows.get(rowOf(sel));
  if (target) target.scrollIntoView({ block: 'nearest' });
}

// ---------- sessions, history

function newSession(path, text, mtime) {
  const s = { path, mtime, savedText: '', text: '', history: [], index: 0, doc: null, parseError: null,
    collapsed: new Set(), sel: null, rawText: text };
  const normalized = text.replace(/\r\n/g, '\n');
  const p = parseXml(normalized);
  if (p.error) {
    s.parseError = p.error;
    s.savedText = s.text = normalized;
  } else {
    s.doc = p.doc;
    s.savedText = s.text = serialize(p.doc);
  }
  s.history = [{ text: s.text, sel: null }];
  return s;
}
function isDirty(s) { return !!s && s.text !== s.savedText; }
function anyDirty() { for (const s of sessions.values()) if (isDirty(s)) return true; return false; }

function touch() {
  invalidate();
  if (cur && cur.doc) cur.text = serialize(cur.doc);
  renderOutline();
  scheduleValidate();
  refreshChrome();
  if (sourceOpen && !sourceDirty) fillSource();
}
function commit() {
  if (!cur || !cur.doc) return;
  invalidate();
  const text = serialize(cur.doc);
  cur.text = text;
  const top = cur.history[cur.index];
  if (top && top.text === text) { top.sel = locate(sel); refreshChrome(); return; }
  cur.history.splice(cur.index + 1);
  cur.history.push({ text, sel: locate(sel) });
  if (cur.history.length > 400) cur.history.shift();
  cur.index = cur.history.length - 1;
  refreshChrome();
  if (sourceOpen && !sourceDirty) fillSource();
}
// fn changes the model and returns the element to select (undefined: keep the selection)
function mutate(fn) {
  if (!cur || !cur.doc) return;
  invalidate();
  const next = fn();
  invalidate();
  sel = normalizeSel(next === undefined ? sel : next) || root();
  cur.sel = sel;
  commit();
  renderAll();
  scrollToSel();
}
// the selection when it contains the edited element (editing in the owner's details), else the element
function keepOr(target) { return sel && sel !== target && sel.contains(target) ? sel : target; }
function loadText(text, selPath) {
  const p = parseXml(text);
  if (p.error) { toast(p.error, 'error'); return false; }
  cur.doc = D = p.doc;
  cur.parseError = null;
  invalidate();
  cur.text = serialize(D);
  sel = resolveLoc(selPath) || null;
  cur.sel = sel;
  return true;
}
function undo() {
  if (!cur || !cur.doc) return;
  if (serialize(cur.doc) !== cur.history[cur.index].text) commit();
  if (cur.index <= 0) return;
  const selPath = cur.history[cur.index].sel;
  cur.index--;
  loadText(cur.history[cur.index].text, selPath || cur.history[cur.index].sel);
  renderAll();
}
function redo() {
  if (!cur || !cur.doc || cur.index >= cur.history.length - 1) return;
  cur.index++;
  loadText(cur.history[cur.index].text, cur.history[cur.index].sel);
  renderAll();
}

// ---------- references kept across moves and renames

function captureRefs() {
  const refs = [];
  for (const el of D.documentElement.getElementsByTagName('*')) {
    for (const a of PATH_ATTRS) {
      if (!el.hasAttribute(a)) continue;
      const v = el.getAttribute(a);
      if (a === 'target') {
        const r = resolveTarget(v);
        if (r.state) refs.push({ el, a, kind: 'state', target: r.state, full: v.split('|').length > 2 });
      } else {
        const r = resolveMember(v, a === 'source' ? 'delegate' : 'property');
        if (r.task) refs.push({ el, a, kind: 'member', target: r.task, member: r.member, named: r.name !== undefined });
      }
    }
  }
  return refs;
}
function restoreRefs(refs) {
  invalidate();
  for (const ref of refs) {
    if (!D.documentElement.contains(ref.el) || !D.documentElement.contains(ref.target)) continue;
    const v = ref.el.getAttribute(ref.a);
    if (ref.kind === 'state') {
      const r = resolveTarget(v);
      if (r.state === ref.target) continue;
      ref.el.setAttribute(ref.a, ref.full ? statePath(ref.target) : 'Root|' + attr(ref.target, 'name'));
    } else {
      const r = resolveMember(v, ref.a === 'source' ? 'delegate' : 'property');
      if (r.task === ref.target) continue;
      const t = ref.target;
      const st = ownerState(t);
      const scope = st ? tasksOf(st) : globalTasks();
      const entry = taskRefs(scope).find((e) => e.task === t);
      let clsRef = entry ? entry.ref : attr(t, 'class');
      if (ref.named && has(t, 'name')) clsRef = attr(t, 'class') + '[' + attr(t, 'name') + ']';
      ref.el.setAttribute(ref.a, (st ? 'Root|' + attr(st, 'name') + '|' : 'Root|') + clsRef + '::' + ref.member);
    }
  }
  invalidate();
}
function renameStateRefs(scope, map) {
  const elements = [scope].concat(Array.from(scope.getElementsByTagName('*')));
  for (const el of elements) {
    for (const a of PATH_ATTRS) {
      if (!el.hasAttribute(a)) continue;
      const segs = el.getAttribute(a).split('|');
      if (segs[0] !== 'Root' || segs.length < 2) continue;
      const end = segs[segs.length - 1].includes('::') ? segs.length - 1 : segs.length;
      let changed = false;
      for (let i = 1; i < end; i++) {
        if (map.has(segs[i])) { segs[i] = map.get(segs[i]); changed = true; }
      }
      if (changed) el.setAttribute(a, segs.join('|'));
    }
    if (el.tagName === 'State' && el.hasAttribute('linkedSubtree') && map.has(el.getAttribute('linkedSubtree'))) {
      el.setAttribute('linkedSubtree', map.get(el.getAttribute('linkedSubtree')));
    }
  }
  invalidate();
}
function uniqueStateName(base, taken) {
  const names = taken || new Set(allStates().map((s) => attr(s, 'name')));
  let name = base;
  let i = 2;
  const stem = base.replace(/ \d+$/, '');
  while (names.has(name)) name = stem + ' ' + i++;
  names.add(name);
  return name;
}

// ---------- operations

function familyOf(el) {
  if (CONDITION_TAGS.includes(el.tagName)) return CONDITION_TAGS;
  return [el.tagName];
}
function moveSibling(el, dir) {
  const sibs = kids(el.parentNode, familyOf(el));
  const i = sibs.indexOf(el);
  const j = i + dir;
  if (i < 0 || j < 0 || j >= sibs.length) return false;
  const refs = captureRefs();
  if (dir < 0) el.parentNode.insertBefore(el, sibs[j]);
  else el.parentNode.insertBefore(el, sibs[j].nextSibling);
  restoreRefs(refs);
  return true;
}
function opMove(el, dir) {
  if (!el || el === root()) return;
  mutate(() => { moveSibling(el, dir); return keepOr(el); });
}
function opDelete(el) {
  if (!el || el === root()) return;
  const sibs = kids(el.parentNode, familyOf(el));
  const i = sibs.indexOf(el);
  const next = keepOr(sibs[i + 1] || sibs[i - 1] || ownerOf(el, ['State', 'Task', 'Transition', 'Consideration']) || root());
  mutate(() => {
    const parent = el.parentNode;
    el.remove();
    removeIfEmpty(parent);
    if (parent.tagName === 'Any' || parent.tagName === 'All') {
      if (!kids(parent).length) { const gp = parent.parentNode; parent.remove(); removeIfEmpty(gp); }
    }
    return next;
  });
}
function opDuplicate(el) {
  if (!el || el === root()) return;
  mutate(() => {
    const copy = el.cloneNode(true);
    if (el.tagName === 'State') {
      const taken = new Set(allStates().map((s) => attr(s, 'name')));
      const map = new Map();
      for (const s of [copy].concat(Array.from(copy.getElementsByTagName('State')))) {
        const name = uniqueStateName(attr(s, 'name') || 'State', taken);
        map.set(attr(s, 'name'), name);
        s.setAttribute('name', name);
      }
      renameStateRefs(copy, map);
    }
    el.parentNode.insertBefore(copy, el.nextSibling);
    return keepOr(copy);
  });
}
function opToggleEnabled(el) {
  if (!el) return;
  mutate(() => { if (attr(el, 'enabled') === 'false') el.removeAttribute('enabled'); else setAttr(el, 'enabled', 'false'); return keepOr(el); });
}
function opAddState(parent, after) {
  mutate(() => {
    const s = mk('State', { name: uniqueStateName('New State') });
    if (after) after.parentNode.insertBefore(s, after.nextSibling);
    else parent.appendChild(s);
    if (parent !== root() && !after) cur.collapsed.delete(attr(parent, 'name'));
    return s;
  });
  setTimeout(() => { const i = $('#details [data-focus="name"]'); if (i) { i.focus(); i.select(); } }, 0);
}
function opAddTask(owner, cls) {
  mutate(() => {
    const container = owner === root() ? ensureContainer(root(), 'GlobalTasks') : ensureContainer(owner, 'Tasks');
    const t = mk('Task', { class: cls });
    container.appendChild(t);
    return t;
  });
}
function opAddTransition(state) {
  mutate(() => {
    const t = mk('Transition', { trigger: 'OnStateCompleted', target: 'Root' });
    ensureContainer(state, 'Transitions').appendChild(t);
    return t;
  });
}
function conditionContainer(owner) {
  if (owner.tagName === 'State') return ensureContainer(owner, 'EnterConditions');
  if (owner.tagName === 'Transition') return ensureContainer(owner, 'Conditions');
  if (owner.tagName === 'Any' || owner.tagName === 'All') return owner;
  let c = kid(owner, 'Conditions');
  if (!c) { c = mk('Conditions'); owner.appendChild(c); }
  return c;
}
function newCondition(cls) {
  const c = mk('Condition', { class: cls || 'Condition_Compare' });
  for (const a of (schema.conditions[cls || 'Condition_Compare'] || {}).attributes || []) if (a.required) c.setAttribute(a.name, '');
  return c;
}
function opAddCondition(owner, cls) {
  mutate(() => keepOr(conditionContainer(owner).appendChild(newCondition(cls))));
}
function opAddGroup(owner, tag) {
  mutate(() => {
    const g = mk(tag);
    g.appendChild(newCondition());
    conditionContainer(owner).appendChild(g);
    return keepOr(g.firstElementChild);
  });
}
function opWrap(el, tag) {
  mutate(() => {
    const g = mk(tag);
    el.parentNode.insertBefore(g, el);
    g.appendChild(el);
    return keepOr(el);
  });
}
function opUnwrap(group) {
  const keep = sel && sel !== group && sel.contains(group) ? sel : null;
  mutate(() => {
    const first = group.firstElementChild;
    while (group.firstChild) group.parentNode.insertBefore(group.firstChild, group);
    group.remove();
    return keep || first;
  });
}
function opInvert(c) {
  mutate(() => { if (attr(c, 'inverted') === 'true') c.removeAttribute('inverted'); else setAttr(c, 'inverted', 'true'); return keepOr(c); });
}
function opAddConsideration(state, cls) {
  mutate(() => {
    const c = mk('Consideration', { class: cls });
    for (const a of (schema.considerations[cls] || {}).attributes || []) if (a.required) c.setAttribute(a.name, '');
    ensureContainer(state, 'Considerations').appendChild(c);
    return keepOr(c);
  });
}
function insertTaskChild(task, el) {
  if (el.tagName === 'Binding') {
    const bindings = kids(task, 'Binding');
    const ref = bindings.length ? bindings[bindings.length - 1].nextSibling : kid(task, 'Conditions');
    task.insertBefore(el, ref || null);
    return el;
  }
  const same = kids(task, el.tagName);
  const ref = same.length ? same[same.length - 1].nextSibling : (kid(task, 'Binding') || kid(task, 'Conditions'));
  task.insertBefore(el, ref || null);
  return el;
}
function settingsNodes(task) {
  return Array.from(task.childNodes).filter((n) => (n.nodeType === 1 && !TASK_STRUCTURE.includes(n.tagName)) || n.nodeType === 8);
}
function settingsText(task) {
  const out = [];
  for (const n of settingsNodes(task)) writeNode(n, 0, out);
  return out.join('\n');
}
function applySettingsText(task, text) {
  const p = parseXml('<Settings>' + text + '</Settings>');
  if (p.error) return p.error;
  for (const n of p.doc.documentElement.children) {
    if (TASK_STRUCTURE.includes(n.tagName)) return '<' + n.tagName + '> is not a setting (edit it in its own section)';
  }
  const old = settingsNodes(task);
  const ref = old.length ? old[0] : (kid(task, 'Binding') || kid(task, 'Conditions'));
  for (const n of Array.from(p.doc.documentElement.childNodes)) {
    if (n.nodeType === 1 || n.nodeType === 8) task.insertBefore(D.importNode(n, true), ref || null);
  }
  for (const n of old) n.remove();
  return null;
}
function moveState(el, target, zone) {
  const refs = captureRefs();
  const oldParent = el.parentNode;
  if (zone === 'inside') {
    target.appendChild(el);
    if (target.tagName === 'State') cur.collapsed.delete(attr(target, 'name'));
  } else if (zone === 'before') target.parentNode.insertBefore(el, target);
  else target.parentNode.insertBefore(el, target.nextSibling);
  removeIfEmpty(oldParent);
  restoreRefs(refs);
}
function moveTask(el, target, zone) {
  const refs = captureRefs();
  const oldParent = el.parentNode;
  if (zone === 'inside') {
    const container = target === root() ? ensureContainer(root(), 'GlobalTasks') : ensureContainer(target, 'Tasks');
    container.appendChild(el);
  } else if (zone === 'before') target.parentNode.insertBefore(el, target);
  else target.parentNode.insertBefore(el, target.nextSibling);
  removeIfEmpty(oldParent);
  restoreRefs(refs);
}

// ---------- validation (mirrors the server loader)

function validateDoc() {
  const out = [];
  const P = (sev, el, msg) => out.push({ sev, el, msg });
  const r = root();
  if (!r) return out;
  if (r.tagName !== 'StateTree') {
    P('error', r, r.tagName === 'Root' ? 'v1 file (<Root>): convert it with tools/StateTreeEditor/convert_v1_to_v2.py'
      : 'the root element must be <StateTree version="2">');
    return out;
  }
  if (attr(r, 'version') !== '2') P('error', r, "<StateTree> version must be 2, got '" + attr(r, 'version') + "'");
  let globalsCount = 0;
  for (const c of kids(r)) {
    if (c.tagName === 'GlobalTasks') { if (++globalsCount > 1) P('error', c, 'more than one <GlobalTasks>'); }
    else if (c.tagName !== 'Comment' && c.tagName !== 'State') P('error', c, 'unexpected <' + c.tagName + '> in <StateTree>');
  }
  if (!kids(r, 'State').length) P('error', r, 'the tree needs at least one top level <State>');

  const bool = (el, name) => {
    if (has(el, name) && !BOOLS.includes(attr(el, name))) P('error', el, name + " must be true or false, got '" + attr(el, name) + "'");
  };
  const enumAttr = (el, name, values) => {
    if (has(el, name) && !values.includes(attr(el, name))) P('error', el, name + " must be one of " + values.join(', ') + ", got '" + attr(el, name) + "'");
  };
  const checkFile = (filename, el) => {
    const rel = String(filename).replace(/\\/g, '/').replace(/^\.\//, '').replace(/^statetree\//, '');
    if (!files.some((f) => f.path === rel)) P('warning', el, "subtree file '" + filename + "' not found under statetree/");
    else if (cur && rel === cur.path) P('error', el, 'the tree runs itself as its subtree');
  };

  function validateConditionEntries(container, inGroup) {
    if (!inGroup) enumAttr(container, 'match', ['all', 'any']);
    for (const c of kids(container)) {
      if (c.tagName === 'Condition') validateCondition(c);
      else if (c.tagName === 'Any' || c.tagName === 'All') {
        if (!kids(c, CONDITION_TAGS).length) P('warning', c, '<' + c.tagName + '> without entries (always ' + (c.tagName === 'Any' ? 'fails' : 'passes') + ')');
        validateConditionEntries(c, true);
      } else {
        P('error', c, 'unexpected <' + c.tagName + '> in <' + container.tagName + '> (expected <Condition>, <Any> or <All>)');
      }
    }
  }
  function checkClassAttributes(el, def) {
    for (const a of def.attributes || []) {
      const v = attr(el, a.name, null);
      if (v === null || v === '') {
        if (a.required) P('error', el, el.tagName + ' ' + attr(el, 'class') + ' needs ' + a.name + '="..."');
        continue;
      }
      if (a.kind === 'number') {
        if (!isNum(v)) P('error', el, a.name + " needs a number, got '" + v + "'");
        else if ((a.min !== undefined && Number(v) < a.min) || (a.max !== undefined && Number(v) > a.max)) {
          P('error', el, a.name + ' must be ' + a.min + '..' + a.max + ", got '" + v + "'");
        }
      } else if (a.kind === 'enum' && a.options && !a.options.includes(v)) {
        P('error', el, a.name + ' must be one of ' + a.options.join(', ') + ", got '" + v + "'");
      } else if (a.kind === 'flags' && a.options) {
        for (const item of v.split(',').map((s) => s.trim())) {
          if (!a.options.includes(item)) P('error', el, a.name + ' takes ' + a.options.join(', ') + " (comma separated), got '" + item + "'");
        }
      }
    }
  }
  function validateCondition(c) {
    const cls = attr(c, 'class');
    bool(c, 'inverted');
    if (has(c, 'negate')) P('warning', c, 'negate is v1: use inverted="true"');
    if (!cls) { P('error', c, '<Condition> needs class="..." (e.g. Condition_Compare)'); return; }
    const def = schema.conditions[cls];
    if (!def) { P('error', c, "unknown condition class '" + cls + "'"); return; }
    checkClassAttributes(c, def);
    for (const k of kids(c)) if (k.tagName !== 'Comment') P('error', k, 'unexpected <' + k.tagName + '> in <Condition>');
    if (cls === 'Condition_Compare') {
      const binding = attr(c, 'binding');
      if (!binding) return;
      const r = resolveMember(binding, 'property');
      if (r.error) { P('error', c, "binding '" + binding + "': " + r.error); return; }
      const traits = r.prop.traits || [];
      const compare = attr(c, 'compare');
      const value = attr(c, 'value');
      if (has(c, 'key')) {
        if (!traits.includes('numberMap')) P('error', c, "binding '" + binding + "' (" + r.prop.type + ') with a key is not a map of numbers');
        if (compare && !isNum(value)) P('error', c, "compare on a map entry needs a number in value, got '" + value + "'");
      } else if (compare) {
        if (!has(c, 'value')) P('error', c, 'compare needs value="..."');
        else if (!isNum(value)) {
          if (compare !== 'equal') P('error', c, "compare needs a number in value=\"...\" (a text only with compare=\"equal\"), got '" + value + "'");
          else if (!traits.includes('text')) P('error', c, "binding '" + binding + "' (" + r.prop.type + ") is not a text, it cannot be compared with '" + value + "'");
        } else if (!traits.includes('number')) {
          P('error', c, "binding '" + binding + "' (" + r.prop.type + ') is not a number, it cannot be compared');
        }
      } else {
        if (has(c, 'value')) P('warning', c, 'value without compare is ignored');
        if (!traits.includes('testable')) P('error', c, "binding '" + binding + "' (" + r.prop.type + ') has a type that cannot be tested (not a container, not bool)');
      }
      if (r.task && attr(r.task, 'enabled') === 'false') P('warning', c, "binding '" + binding + "' reads a disabled task");
    }
  }
  function validateConsideration(c, state) {
    const cls = attr(c, 'class');
    if (!cls) { P('error', c, '<Consideration> needs class="..."'); return; }
    const def = schema.considerations[cls];
    if (!def) { P('error', c, "unknown consideration class '" + cls + "'"); return; }
    checkClassAttributes(c, def);
    if (cls === 'Consideration_Number') {
      const binding = attr(c, 'binding');
      if (binding) {
        const r = resolveMember(binding, 'property');
        if (r.error) P('error', c, "binding '" + binding + "': " + r.error);
        else if (!(r.prop.traits || []).includes('number')) P('error', c, "binding '" + binding + "' (" + r.prop.type + ') is not a number');
      }
      if (isNum(attr(c, 'min', '0')) && isNum(attr(c, 'max', '1')) && Number(attr(c, 'min', '0')) === Number(attr(c, 'max', '1'))) {
        P('error', c, 'min and max are equal');
      }
      let lastX = -Infinity;
      for (const k of kids(c, 'Key')) {
        if (!isNum(attr(k, 'x')) || !isNum(attr(k, 'y'))) P('error', k, '<Key> needs numbers in x and y');
        else if (Number(attr(k, 'x')) < lastX) P('warning', k, '<Key> x values should ascend');
        else lastX = Number(attr(k, 'x'));
      }
    }
    for (const k of kids(c)) if (k.tagName !== 'Comment' && !(def.children && def.children[k.tagName])) P('error', k, 'unexpected <' + k.tagName + '> in <Consideration>');
    const parent = state.parentNode;
    if (parent && parent.tagName === 'StateTree') P('info', c, 'the implicit Root selects the top level states in order: their considerations are not used');
    else if (parent && parent.tagName === 'State' && !UTILITY_SELECTIONS.includes(attr(parent, 'selection', 'TrySelectChildrenInOrder'))) {
      P('info', c, "considerations are only used by a ...Utility selection of the parent ('" + attr(parent, 'name') + "' selects " + attr(parent, 'selection', 'TrySelectChildrenInOrder') + ')');
    }
  }
  function validateTask(t, isGlobal) {
    const cls = attr(t, 'class');
    const def = taskDef(cls);
    if (!cls) P('error', t, '<Task> needs class="..."');
    else if (!def) P('error', t, "unknown task class '" + cls + "'");
    else if (!isGlobal && def.global) P('info', t, cls + ' is a global task class (normally in <GlobalTasks>)');
    enumAttr(t, 'completion', COMPLETIONS);
    bool(t, 'show_log');
    bool(t, 'enabled');
    if (/[|\]\[]/.test(attr(t, 'name'))) P('error', t, "a task name cannot contain '|', '[' or ']'");
    const locals = new Set();
    for (const c of kids(t)) {
      if (c.tagName === 'Binding') {
        const local = attr(c, 'local');
        const external = attr(c, 'external');
        if (!local || !external) { P('error', c, '<Binding> needs local="<property>" and external="Root|..."'); continue; }
        if (locals.has(local)) P('warning', c, "property '" + local + "' is bound twice");
        locals.add(local);
        const lp = propOf(def, local);
        if (def && !lp) P('error', c, "<Binding> local '" + local + "' is not a property of " + cls);
        const r = resolveMember(external, 'property');
        if (r.error) P('error', c, "<Binding> external '" + external + "': " + r.error);
        else if (lp && canonType(lp.type) !== canonType(r.prop.type)) {
          P('error', c, "<Binding> '" + local + "' (" + lp.type + ") and '" + external + "' (" + r.prop.type + ') have different types');
        }
        if (r.task === t) P('error', c, '<Binding> reads the task itself');
      } else if (c.tagName === 'Conditions') {
        if (!isGlobal) P('error', c, '<Conditions> in a <Task> only in <GlobalTasks> (a state has <EnterConditions>)');
        else validateConditionEntries(c, false);
      }
    }
    if (cls === 'Task_RunSubtree') {
      const st = kid(t, 'Subtree');
      if (!st || !attr(st, 'filename')) P('error', t, 'Task_RunSubtree needs <Subtree filename="statetree/..."/>');
      else checkFile(attr(st, 'filename'), st);
    }
    if (def) {
      const known = new Set(KNOWN_ATTRS.Task.concat(Object.keys(def.taskAttributes || {})));
      for (const a of t.attributes) if (!known.has(a.name)) P('info', t, "attribute '" + a.name + "' is not known for " + cls);
    }
  }
  function validateTransition(tr, state) {
    const trigger = attr(tr, 'trigger');
    if (!trigger) P('error', tr, '<Transition> needs trigger="..."');
    else if (!TRIGGERS.includes(trigger)) P('error', tr, 'trigger must be one of ' + TRIGGERS.join(', ') + ", got '" + trigger + "'");
    if (has(tr, 'type') || has(tr, 'node')) P('error', tr, 'type / node are v1: use trigger / target');
    const target = attr(tr, 'target');
    if (!target) P('error', tr, '<Transition> needs target="..."');
    else {
      const r = resolveTarget(target);
      if (r.error) P('error', tr, "target '" + target + "': " + r.error);
      else if (r.state && attr(r.state, 'enabled') === 'false') P('warning', tr, "target '" + target + "' is disabled");
      else if (r.special === 'NextState') {
        let next = state.nextElementSibling;
        while (next && next.tagName !== 'State') next = next.nextElementSibling;
        if (!next) P('error', tr, "NextState: '" + attr(state, 'name') + "' has no next sibling state");
      }
    }
    if (trigger === 'OnDelegate') {
      if (!attr(tr, 'source')) P('error', tr, 'OnDelegate needs source="Root|<TaskClass>::<delegate>"');
      else {
        const r = resolveMember(attr(tr, 'source'), 'delegate');
        if (r.error) P('error', tr, "source '" + attr(tr, 'source') + "': " + r.error);
      }
    } else if (has(tr, 'source')) P('warning', tr, 'source is only used by OnDelegate');
    if (trigger === 'OnEvent') { if (!attr(tr, 'event')) P('error', tr, 'OnEvent needs event="<tag>"'); }
    else if (has(tr, 'event')) P('warning', tr, 'event is only used by OnEvent');
    enumAttr(tr, 'priority', PRIORITIES);
    if (has(tr, 'delay') && !isUInt(attr(tr, 'delay'))) P('error', tr, "delay needs ms (a whole number), got '" + attr(tr, 'delay') + "'");
    bool(tr, 'enabled');
    for (const c of kids(tr)) {
      if (c.tagName === 'Conditions') validateConditionEntries(c, false);
      else if (c.tagName !== 'Comment') P('error', c, 'unexpected <' + c.tagName + '> in <Transition> (expected <Conditions>)');
    }
  }
  function validateState(s) {
    const type = attr(s, 'type', 'State');
    enumAttr(s, 'type', STATE_TYPES);
    enumAttr(s, 'selection', SELECTIONS);
    bool(s, 'enabled');
    if (has(s, 'weight') && (!isNum(attr(s, 'weight')) || Number(attr(s, 'weight')) < 0)) P('error', s, "weight needs a number >= 0, got '" + attr(s, 'weight') + "'");
    if (has(s, 'retryDelay') && !isUInt(attr(s, 'retryDelay'))) P('error', s, "retryDelay needs ms (a whole number), got '" + attr(s, 'retryDelay') + "'");
    const children = childStates(s);
    if (type === 'Linked') {
      const name = attr(s, 'linkedSubtree');
      const target = name ? stateByName(name) : null;
      if (!name) P('error', s, 'a Linked state needs linkedSubtree="<Subtree state>"');
      else if (!target) P('error', s, "linkedSubtree '" + name + "' is no state of the tree");
      else if (attr(target, 'type') !== 'Subtree') P('error', s, "linkedSubtree '" + name + "' is not of type Subtree");
      else if (target === s || target.contains(s)) P('error', s, 'a Linked state cannot run a subtree containing itself');
      if (children.length) P('warning', s, 'a Linked state runs the children of its subtree: its own child states are ignored');
    } else if (has(s, 'linkedSubtree')) P('warning', s, 'linkedSubtree is only used by Linked states');
    if (!children.length && type !== 'Linked') {
      const tasks = tasksOf(s);
      if (!tasks.length) P('error', s, "state '" + attr(s, 'name') + "' has no child states and no task");
      else if (tasks.every((t) => attr(t, 'enabled') === 'false')) P('warning', s, 'every task of the state is disabled');
    }
    if (attr(s, 'selection') === 'TryFollowTransitions') {
      const usable = kids(kid(s, 'Transitions'), 'Transition').some((tr) => {
        if (['OnDelegate', 'OnEvent'].includes(attr(tr, 'trigger'))) return false;
        const r = resolveTarget(attr(tr, 'target'));
        return !!r.state && r.state !== s && s.contains(r.state);
      });
      if (!usable) P('warning', s, 'TryFollowTransitions: no transition to a state below it (trigger other than OnDelegate / OnEvent)');
    }
    if (has(s, 'selection') && !children.length && type !== 'Linked') P('info', s, 'selection has no effect on a state without children');
    for (const c of kids(s)) {
      switch (c.tagName) {
        case 'Comment': case 'State': break;
        case 'EnterConditions': validateConditionEntries(c, false); break;
        case 'Considerations':
          for (const k of kids(c)) {
            if (k.tagName === 'Consideration') validateConsideration(k, s);
            else P('error', k, 'unexpected <' + k.tagName + '> in <Considerations>');
          }
          break;
        case 'Tasks':
          for (const t of kids(c)) {
            if (t.tagName === 'Task') validateTask(t, false);
            else P('error', t, 'unexpected <' + t.tagName + '> in <Tasks>');
          }
          break;
        case 'Transitions':
          for (const t of kids(c)) {
            if (t.tagName === 'Transition') validateTransition(t, s);
            else P('error', t, 'unexpected <' + t.tagName + '> in <Transitions>');
          }
          break;
        default:
          P('error', c, 'unexpected <' + c.tagName + '> in <State> (expected EnterConditions, Considerations, Tasks, Transitions, State)');
      }
    }
    const seen = new Map();
    for (const t of tasksOf(s).concat([])) {
      const key = attr(t, 'class') + '[' + attr(t, 'name') + ']';
      if (has(t, 'name') && seen.has(key)) P('warning', t, 'two tasks ' + key + ' in the state: paths only reach the first');
      seen.set(key, t);
    }
  }

  const names = new Map();
  for (const s of allStates()) {
    const n = attr(s, 'name');
    if (!n) P('error', s, '<State> needs name="..."');
    else if (names.has(n)) P('error', s, "state name '" + n + "' is used twice (names are unique in the tree)");
    else names.set(n, s);
    if (n.includes('|')) P('error', s, "a state name cannot contain '|'");
    if (n !== n.trim()) P('warning', s, 'the state name starts or ends with spaces');
  }
  const gseen = new Map();
  for (const g of kids(r, 'GlobalTasks')) {
    for (const c of kids(g)) {
      if (c.tagName === 'Task') {
        validateTask(c, true);
        const key = attr(c, 'class') + '[' + attr(c, 'name') + ']';
        if (has(c, 'name') && gseen.has(key)) P('warning', c, 'two global tasks ' + key + ': paths only reach the first');
        gseen.set(key, c);
      } else P('error', c, 'unexpected <' + c.tagName + '> in <GlobalTasks> (expected <Task class="...">)');
    }
  }
  for (const s of allStates()) validateState(s);
  return out;
}
function scheduleValidate() {
  clearTimeout(validateTimer);
  validateTimer = setTimeout(runValidate, 120);
}
function runValidate() {
  clearTimeout(validateTimer);
  invalidate();
  problems = cur && cur.doc ? validateDoc() : [];
  if (cur) cur.problemCount = problems.filter((p) => p.sev === 'error').length;
  renderProblems();
  markRows();
  renderFileList();
}

// ---------- labels

function shortClass(cls) { return String(cls || '?').replace(/^Task_/, '').replace(/_GlobalTask$/, ''); }
function fmtValue(v) { return isNum(v) ? v : '"' + v + '"'; }
function memberOfPath(p) { const i = String(p).lastIndexOf('::'); return i < 0 ? String(p) : p.slice(i + 2); }
function condLabel(c) {
  if (c.tagName === 'Any' || c.tagName === 'All') {
    const parts = kids(c, CONDITION_TAGS).map(condLabel);
    return '(' + (parts.join(c.tagName === 'Any' ? ' ∨ ' : ' ∧ ') || '') + ')';
  }
  const cls = attr(c, 'class');
  let text;
  if (cls === 'Condition_Compare') {
    const b = attr(c, 'binding');
    text = memberOfPath(b) || '?';
    if (has(c, 'key')) text += '[' + attr(c, 'key') + ']';
    if (has(c, 'compare')) text += ' ' + (COMPARE_OPS[attr(c, 'compare')] || attr(c, 'compare')) + ' ' + fmtValue(attr(c, 'value'));
  } else if (cls === 'Condition_RandomChance') text = 'chance ' + attr(c, 'chance') + '%';
  else if (cls === 'Condition_TimeInState') text = 'time >= ' + attr(c, 'ms') + 'ms';
  else if (cls === 'Condition_ActiveStateLocation') text = 'active ' + attr(c, 'location', 'above,below');
  else text = String(cls || '?').replace(/^Condition_/, '');
  return (attr(c, 'inverted') === 'true' ? '!' : '') + text;
}
function taskInfo(t) {
  if (has(t, 'name')) return attr(t, 'name');
  if (has(t, 'spellid')) return attr(t, 'spellid');
  const d = kid(t, 'Delay');
  if (d) return attr(d, 'value') + 'ms';
  const st = kid(t, 'Subtree');
  if (st) return attr(st, 'filename').replace(/^.*\//, '').replace(/\.xml$/i, '').replace(/^STAI_/, '');
  for (const n of settingsNodes(t)) {
    if (n.nodeType === 1 && n.attributes.length) {
      const count = kids(t, n.tagName).length;
      return n.tagName + ' ' + n.attributes[0].value + (count > 1 ? ' +' + (count - 1) : '');
    }
  }
  return '';
}
function targetLabel(t) { return t.startsWith('Root|') ? t.slice(5) : t; }
function transLabel(tr) {
  const trigger = attr(tr, 'trigger', '?');
  let s = trigger;
  if (trigger === 'OnDelegate') s += ' ' + (memberOfPath(attr(tr, 'source')) || '?');
  if (trigger === 'OnEvent') s += ' "' + attr(tr, 'event') + '"';
  return s;
}
function whereLabel(el) {
  const st = ownerState(el);
  const parts = [st ? attr(st, 'name') : (el === root() ? 'tree' : 'Global tasks')];
  const task = el.tagName === 'Task' ? el : ownerOf(el, ['Task']);
  if (task) parts.push(shortClass(attr(task, 'class')) + (has(task, 'name') ? '[' + attr(task, 'name') + ']' : ''));
  const tr = el.tagName === 'Transition' ? el : ownerOf(el, ['Transition']);
  if (tr) parts.push('Transition');
  if (!task && !tr && el !== st && el !== root()) parts.push(el.tagName);
  return parts.join(' › ');
}

// ---------- outline

function renderOutline() {
  const box = $('#outline');
  const scroll = box.scrollTop;
  box.textContent = '';
  elRows = new Map();
  rowOrder = [];
  if (!cur) { box.append(h('div', { class: 'empty-hint' }, 'Open a tree from the file list, or create one with New.')); return; }
  if (!cur.doc) {
    box.append(h('div', { class: 'empty-hint' }, 'The file does not parse: ' + (cur.parseError || '')),
      h('div', { class: 'empty-hint' }, h('button', { onclick: () => openSource() }, 'Edit the XML source')));
    return;
  }
  invalidate();
  const r = root();
  const head = h('div', { class: 'tree-head' + (sel === r ? ' selected' : ''), onclick: () => select(r),
    oncontextmenu: (e) => { e.preventDefault(); select(r, { scroll: false }); showMenu(e.clientX, e.clientY, treeMenu()); } },
  h('span', { class: 'name' }, cur.path.replace(/^.*\//, '')), h('span', { class: 'badge' }, r.tagName + ' v' + attr(r, 'version')));
  elRows.set(r, head);
  rowOrder.push(r);
  box.append(head);
  const comment = getComment(r);
  if (comment) box.append(h('div', { class: 'tree-comment', title: comment }, comment));
  if (r.tagName !== 'StateTree') return;

  const gHead = h('div', { class: 'section-head' }, h('span', null, 'Global Tasks'), h('span', { class: 'spacer' }),
    h('button', { class: 'icon', title: 'Add global task', onclick: (e) => { e.stopPropagation(); pickTask(r); } }, '+ Task'));
  addDropTarget(gHead, { kind: 'globals' });
  const gSection = h('div', { class: 'section' }, gHead);
  const gl = globalTasks();
  if (!gl.length) gSection.append(h('div', { class: 'empty-hint' }, 'No global tasks.'));
  for (const t of gl) gSection.append(renderGlobalTask(t));
  box.append(gSection);

  const sHead = h('div', { class: 'section-head' }, h('span', null, 'States'), h('span', { class: 'spacer' }),
    h('button', { class: 'icon', title: 'Add top level state', onclick: (e) => { e.stopPropagation(); opAddState(r); } }, '+ State'));
  const sSection = h('div', { class: 'section' }, sHead);
  for (const s of childStates(r)) sSection.append(renderState(s));
  const end = h('div', { class: 'drop-end' });
  addDropTarget(end, { kind: 'end' });
  sSection.append(end);
  box.append(sSection);
  markRows();
  box.scrollTop = scroll;
}
function matchesFind(el) {
  if (!findText) return false;
  const q = findText.toLowerCase();
  if (el.tagName === 'State' && attr(el, 'name').toLowerCase().includes(q)) return true;
  const tasks = el.tagName === 'State' ? tasksOf(el) : [el];
  return tasks.some((t) => (attr(t, 'class') + ' ' + attr(t, 'name') + ' ' + taskInfo(t)).toLowerCase().includes(q));
}
function condChips(container, owner) {
  const wrap = h('span', { class: 'conds' });
  if (!container) return wrap;
  const entries = kids(container, CONDITION_TAGS);
  entries.forEach((c, i) => {
    if (i) wrap.append(h('span', { class: 'cond-op' }, attr(container, 'match') === 'any' ? '∨' : '∧'));
    const chip = h('span', { class: 'chip cond' + (sel === c ? ' selected' : ''), title: condTitle(c),
      onclick: (e) => { e.stopPropagation(); select(c); },
      oncontextmenu: (e) => { e.preventDefault(); e.stopPropagation(); select(c, { scroll: false }); showMenu(e.clientX, e.clientY, conditionMenu(c)); } },
    condLabel(c));
    wrap.append(chip);
  });
  return wrap;
}
function condTitle(c) {
  if (c.tagName !== 'Condition') return c.tagName + ' group';
  const parts = [attr(c, 'class')];
  for (const a of c.attributes) if (a.name !== 'class') parts.push(a.name + '="' + a.value + '"');
  const comment = getComment(c);
  return parts.join('\n') + (comment ? '\n\n' + comment : '');
}
function taskChip(t) {
  const info = taskInfo(t);
  const comment = getComment(t);
  const chip = h('span', { class: 'chip task' + (sel === t ? ' selected' : '') + (attr(t, 'enabled') === 'false' ? ' disabled' : ''),
    title: attr(t, 'class') + (info ? ' — ' + info : '') + (comment ? '\n\n' + comment : ''), draggable: true,
    onclick: (e) => { e.stopPropagation(); select(t); },
    oncontextmenu: (e) => { e.preventDefault(); e.stopPropagation(); select(t, { scroll: false }); showMenu(e.clientX, e.clientY, taskMenu(t)); } },
  shortClass(attr(t, 'class')), info ? h('span', { class: 'info' }, info) : null);
  chip.addEventListener('dragstart', (e) => { e.stopPropagation(); drag = { el: t, kind: 'task' }; e.dataTransfer.effectAllowed = 'move'; e.dataTransfer.setData('text/plain', attr(t, 'class')); });
  chip.addEventListener('dragend', clearDrop);
  addDropTarget(chip, { kind: 'taskChip', el: t });
  elRows.set(t, chip);
  return chip;
}
function renderGlobalTask(t) {
  const comment = getComment(t);
  const conds = kid(t, 'Conditions');
  const row = h('div', { class: 'st-row global' + (sel === t ? ' selected' : '') + (attr(t, 'enabled') === 'false' ? ' disabled' : ''),
    draggable: true, title: comment || attr(t, 'class'),
    onclick: () => select(t),
    oncontextmenu: (e) => { e.preventDefault(); select(t, { scroll: false }); showMenu(e.clientX, e.clientY, taskMenu(t)); } },
  h('span', { class: 'st-name' }, shortClass(attr(t, 'class'))),
  taskInfo(t) ? h('span', { class: 'chip task' }, taskInfo(t)) : null,
  comment ? h('span', { class: 'comment-mark', title: comment }, '✎') : null,
  conds ? h('span', { class: 'cond-op' }, 'while') : null,
  conds ? condChips(conds, t) : null);
  row.addEventListener('dragstart', (e) => { drag = { el: t, kind: 'task' }; e.dataTransfer.effectAllowed = 'move'; e.dataTransfer.setData('text/plain', attr(t, 'class')); });
  row.addEventListener('dragend', clearDrop);
  addDropTarget(row, { kind: 'globalRow', el: t });
  elRows.set(t, row);
  rowOrder.push(t);
  return row;
}
function renderState(s) {
  const node = h('div', { class: 'st-node' });
  const children = childStates(s);
  const name = attr(s, 'name');
  const collapsed = cur.collapsed.has(name);
  const type = attr(s, 'type', 'State');
  const comment = getComment(s);
  const selection = attr(s, 'selection', 'TrySelectChildrenInOrder');
  const cls = ['st-row'];
  if (sel === s) cls.push('selected');
  if (type === 'Subtree') cls.push('subtree');
  if (type === 'Linked') cls.push('linked');
  if (attr(s, 'enabled') === 'false') cls.push('disabled');
  if (matchesFind(s)) cls.push('found');
  const considerations = kids(kid(s, 'Considerations'), 'Consideration');
  const row = h('div', { class: cls.join(' '), draggable: true, title: comment,
    onclick: () => select(s),
    ondblclick: (e) => { if (children.length) { e.preventDefault(); toggleCollapse(s); } },
    oncontextmenu: (e) => { e.preventDefault(); select(s, { scroll: false }); showMenu(e.clientX, e.clientY, stateMenu(s)); } },
  h('span', { class: 'twist', onclick: (e) => { e.stopPropagation(); if (children.length) toggleCollapse(s); } },
    children.length ? (collapsed ? '▸' : '▾') : ''),
  h('span', { class: 'sel-icon', title: children.length || has(s, 'selection') ? selection : 'leaf state' },
    children.length || has(s, 'selection') ? SELECTION_ICONS[selection] || '?' : '•'),
  h('span', { class: 'st-name' }, name || '(unnamed)'),
  type === 'Subtree' ? h('span', { class: 'badge subtree' }, 'Subtree') : null,
  type === 'Linked' ? h('span', { class: 'badge linked', title: 'runs ' + attr(s, 'linkedSubtree') }, 'Linked → ' + attr(s, 'linkedSubtree')) : null,
  comment ? h('span', { class: 'comment-mark', title: comment }, '✎') : null,
  has(s, 'weight') || considerations.length ? h('span', { class: 'chip cons', title: 'utility: weight ' + attr(s, 'weight', '1') + ', ' + plural(considerations.length, 'consideration'),
    onclick: (e) => { e.stopPropagation(); select(considerations[0] || s); } }, 'U' + (has(s, 'weight') ? ' ×' + attr(s, 'weight') : '') + (considerations.length ? ' ' + considerations.length : '')) : null,
  condChips(kid(s, 'EnterConditions'), s),
  h('span', { class: 'tasks' }, tasksOf(s).map(taskChip)));
  row.addEventListener('dragstart', (e) => { drag = { el: s, kind: 'state' }; e.dataTransfer.effectAllowed = 'move'; e.dataTransfer.setData('text/plain', name); });
  row.addEventListener('dragend', clearDrop);
  addDropTarget(row, { kind: 'stateRow', el: s });
  elRows.set(s, row);
  rowOrder.push(s);
  node.append(row);

  const transitions = kids(kid(s, 'Transitions'), 'Transition');
  if (transitions.length) {
    const list = h('div', { class: 'trans-list' });
    for (const tr of transitions) {
      const conds = kid(tr, 'Conditions');
      const extra = [];
      if (has(tr, 'priority') && attr(tr, 'priority') !== 'Normal') extra.push(attr(tr, 'priority'));
      if (has(tr, 'delay') && attr(tr, 'delay') !== '0') extra.push('+' + attr(tr, 'delay') + 'ms');
      const line = h('div', { class: 'trans-line' + (sel === tr ? ' selected' : '') + (attr(tr, 'enabled') === 'false' ? ' disabled' : ''),
        title: [attr(tr, 'trigger'), attr(tr, 'source'), attr(tr, 'event'), '→ ' + attr(tr, 'target')].filter(Boolean).join('\n') + (getComment(tr) ? '\n\n' + getComment(tr) : ''),
        onclick: () => select(tr),
        oncontextmenu: (e) => { e.preventDefault(); select(tr, { scroll: false }); showMenu(e.clientX, e.clientY, transitionMenu(tr)); } },
      h('span', null, transLabel(tr)), h('span', { class: 'arrow' }, '→'), h('span', { class: 'target' }, targetLabel(attr(tr, 'target', '?'))),
      extra.length ? h('span', { class: 'arrow' }, '[' + extra.join(', ') + ']') : null,
      conds ? h('span', { class: 'arrow' }, 'if') : null, conds ? condChips(conds, tr) : null);
      elRows.set(tr, line);
      list.append(line);
    }
    node.append(list);
  }
  if (children.length && !collapsed) {
    const box = h('div', { class: 'st-children' });
    for (const c of children) box.append(renderState(c));
    node.append(box);
  }
  return node;
}
function markRows() {
  for (const row of elRows.values()) row.classList.remove('has-error');
  for (const p of problems) {
    if (p.sev !== 'error' || !p.el) continue;
    const r = rowOf(p.el);
    const row = r && elRows.get(r);
    if (row && row.classList.contains('st-row')) row.classList.add('has-error');
  }
}
function toggleCollapse(s) {
  const name = attr(s, 'name');
  if (cur.collapsed.has(name)) cur.collapsed.delete(name); else cur.collapsed.add(name);
  if (sel && s.contains(sel) && sel !== s && cur.collapsed.has(name)) { sel = s; cur.sel = s; renderDetails(); }
  renderOutline();
}
function setAllCollapsed(collapsed) {
  if (!cur || !cur.doc) return;
  cur.collapsed.clear();
  if (collapsed) for (const s of allStates()) if (childStates(s).length) cur.collapsed.add(attr(s, 'name'));
  if (collapsed && sel && rowOf(sel) && rowOf(sel).tagName === 'State') {
    let top = rowOf(sel);
    while (top.parentNode && top.parentNode.tagName === 'State') top = top.parentNode;
    if (top !== sel) select(top); else renderOutline();
  } else renderOutline();
}

// ---------- drag and drop

function clearDrop() {
  for (const el of document.querySelectorAll('.drop-before, .drop-after, .drop-inside')) el.classList.remove('drop-before', 'drop-after', 'drop-inside');
}
function dropZone(e, el, info) {
  if (!drag) return null;
  const rect = el.getBoundingClientRect();
  const y = (e.clientY - rect.top) / rect.height;
  const x = (e.clientX - rect.left) / rect.width;
  if (drag.kind === 'state') {
    if (info.kind === 'end') return 'end';
    if (info.kind !== 'stateRow') return null;
    if (drag.el === info.el || drag.el.contains(info.el)) return null;
    return y < 0.28 ? 'before' : y > 0.72 ? 'after' : 'inside';
  }
  if (drag.kind === 'task') {
    if (info.kind === 'stateRow') return 'inside';
    if (info.kind === 'globals') return 'inside';
    if (info.kind === 'globalRow') return info.el === drag.el ? null : (y < 0.5 ? 'before' : 'after');
    if (info.kind === 'taskChip') return info.el === drag.el ? null : (x < 0.5 ? 'before' : 'after');
  }
  return null;
}
function addDropTarget(el, info) {
  el.addEventListener('dragover', (e) => {
    const zone = dropZone(e, el, info);
    if (!zone) return;
    e.preventDefault();
    e.stopPropagation();
    e.dataTransfer.dropEffect = 'move';
    clearDrop();
    el.classList.add(zone === 'end' ? 'drop-inside' : 'drop-' + zone);
  });
  el.addEventListener('dragleave', () => el.classList.remove('drop-before', 'drop-after', 'drop-inside'));
  el.addEventListener('drop', (e) => {
    const zone = dropZone(e, el, info);
    clearDrop();
    if (!zone) return;
    e.preventDefault();
    e.stopPropagation();
    const d = drag;
    drag = null;
    mutate(() => {
      if (d.kind === 'state') {
        if (zone === 'end') moveState(d.el, root(), 'inside');
        else moveState(d.el, info.el, zone);
      } else if (info.kind === 'stateRow') moveTask(d.el, info.el, 'inside');
      else if (info.kind === 'globals') moveTask(d.el, root(), 'inside');
      else moveTask(d.el, info.el, zone);
      return d.el;
    });
  });
}

// ---------- menus

function stateMenu(s) {
  return [
    { label: 'Add Child State', key: 'Ins', action: () => opAddState(s) },
    { label: 'Add Sibling State', key: 'Shift+Ins', action: () => opAddState(s.parentNode, s) },
    { label: 'Add Task…', key: 'T', action: () => pickTask(s) },
    { label: 'Add Transition', action: () => opAddTransition(s) },
    { label: 'Add Enter Condition', action: () => opAddCondition(s) },
    { label: 'Add Consideration…', action: () => pickConsideration(s) },
    '-',
    { label: 'Duplicate', key: 'Ctrl+D', action: () => opDuplicate(s) },
    { label: 'Move Up', key: 'Alt+↑', action: () => opMove(s, -1) },
    { label: 'Move Down', key: 'Alt+↓', action: () => opMove(s, 1) },
    { label: attr(s, 'enabled') === 'false' ? 'Enable' : 'Disable', key: 'Ctrl+E', action: () => opToggleEnabled(s) },
    '-',
    { label: 'Delete', key: 'Del', action: () => opDelete(s) },
  ];
}
function taskMenu(t) {
  const items = [
    { label: 'Duplicate', key: 'Ctrl+D', action: () => opDuplicate(t) },
    { label: 'Move Up', key: 'Alt+↑', action: () => opMove(t, -1) },
    { label: 'Move Down', key: 'Alt+↓', action: () => opMove(t, 1) },
    { label: attr(t, 'enabled') === 'false' ? 'Enable' : 'Disable', key: 'Ctrl+E', action: () => opToggleEnabled(t) },
  ];
  if (isGlobalTask(t)) items.push({ label: 'Add Condition', action: () => opAddCondition(t) });
  items.push('-', { label: 'Delete', key: 'Del', action: () => opDelete(t) });
  return items;
}
function transitionMenu(tr) {
  return [
    { label: 'Add Condition', action: () => opAddCondition(tr) },
    { label: 'Duplicate', key: 'Ctrl+D', action: () => opDuplicate(tr) },
    { label: 'Move Up', key: 'Alt+↑', action: () => opMove(tr, -1) },
    { label: 'Move Down', key: 'Alt+↓', action: () => opMove(tr, 1) },
    { label: attr(tr, 'enabled') === 'false' ? 'Enable' : 'Disable', key: 'Ctrl+E', action: () => opToggleEnabled(tr) },
    '-',
    { label: 'Delete', key: 'Del', action: () => opDelete(tr) },
  ];
}
function conditionMenu(c) {
  const items = [];
  if (c.tagName === 'Condition') items.push({ label: attr(c, 'inverted') === 'true' ? 'Remove Invert' : 'Invert', key: 'I', action: () => opInvert(c) });
  else items.push({ label: 'Unwrap Group', action: () => opUnwrap(c) }, { label: 'Add Condition', action: () => opAddCondition(c) });
  items.push({ label: 'Wrap in Any', action: () => opWrap(c, 'Any') }, { label: 'Wrap in All', action: () => opWrap(c, 'All') },
    { label: 'Duplicate', key: 'Ctrl+D', action: () => opDuplicate(c) },
    { label: 'Move Up', key: 'Alt+↑', action: () => opMove(c, -1) },
    { label: 'Move Down', key: 'Alt+↓', action: () => opMove(c, 1) },
    '-', { label: 'Delete', key: 'Del', action: () => opDelete(c) });
  return items;
}
function treeMenu() {
  return [
    { label: 'Add Global Task…', action: () => pickTask(root()) },
    { label: 'Add State', action: () => opAddState(root()) },
    '-',
    { label: 'Expand All', action: () => setAllCollapsed(false) },
    { label: 'Collapse All', action: () => setAllCollapsed(true) },
  ];
}
function showMenu(x, y, items) {
  const m = $('#menu');
  m.textContent = '';
  for (const it of items) {
    if (it === '-') { m.append(h('div', { class: 'msep' })); continue; }
    m.append(h('div', { class: 'mi' + (it.disabled ? ' disabled' : ''), onclick: () => { hideMenu(); it.action(); } },
      h('span', null, it.label), it.key ? h('span', { class: 'key' }, it.key) : null));
  }
  m.classList.remove('hidden');
  const r = m.getBoundingClientRect();
  m.style.left = Math.max(4, Math.min(x, window.innerWidth - r.width - 4)) + 'px';
  m.style.top = Math.max(4, Math.min(y, window.innerHeight - r.height - 4)) + 'px';
}
function hideMenu() { $('#menu').classList.add('hidden'); }

// ---------- popups

function openPopup(build, anchor) {
  const p = $('#popup');
  p.textContent = '';
  build(p);
  p.classList.remove('hidden');
  const r = p.getBoundingClientRect();
  p.style.left = Math.max(8, (window.innerWidth - r.width) / 2) + 'px';
  p.style.top = Math.max(8, Math.min(window.innerHeight * 0.15, window.innerHeight - r.height - 8)) + 'px';
  const first = p.querySelector('input, textarea, button');
  if (first) first.focus();
}
function closePopup() { $('#popup').classList.add('hidden'); }
function openPicker(opts) {
  openPopup((p) => {
    const input = h('input', { type: 'search', placeholder: opts.placeholder || 'Filter', spellcheck: false });
    const list = h('div', { class: 'p-list' });
    let active = 0;
    let shown = [];
    const pick = (it) => { closePopup(); opts.onPick(it); };
    const draw = () => {
      const q = input.value.toLowerCase();
      shown = opts.items.filter((it) => !q || (it.label + ' ' + (it.sub || '') + ' ' + (it.group || '')).toLowerCase().includes(q));
      list.textContent = '';
      let group = null;
      shown.forEach((it, i) => {
        if (it.group && it.group !== group) { group = it.group; list.append(h('div', { class: 'p-group' }, group)); }
        const item = h('div', { class: 'p-item' + (i === active ? ' active' : ''), onclick: () => pick(it) },
          h('div', null, it.label), it.sub ? h('div', { class: 'sub', title: it.sub }, it.sub) : null);
        list.append(item);
        if (i === active) setTimeout(() => item.scrollIntoView({ block: 'nearest' }), 0);
      });
      if (!shown.length) list.append(h('div', { class: 'empty-hint' }, 'nothing matches'));
    };
    input.addEventListener('input', () => { active = 0; draw(); });
    input.addEventListener('keydown', (e) => {
      if (e.key === 'ArrowDown') { active = Math.min(active + 1, shown.length - 1); draw(); e.preventDefault(); }
      else if (e.key === 'ArrowUp') { active = Math.max(active - 1, 0); draw(); e.preventDefault(); }
      else if (e.key === 'Enter') { if (shown[active]) pick(shown[active]); e.preventDefault(); }
      else if (e.key === 'Escape') { closePopup(); e.preventDefault(); }
    });
    p.append(h('div', { class: 'p-head' }, opts.title), h('div', { class: 'p-body' }, input, list));
    draw();
  });
}
function openPrompt(opts) {
  openPopup((p) => {
    const input = h('input', { type: 'text', value: opts.value || '', spellcheck: false });
    const error = h('div', { class: 'raw-error' });
    const ok = () => { const msg = opts.onOk(input.value.trim()); if (msg) error.textContent = msg; };
    input.addEventListener('keydown', (e) => {
      if (e.key === 'Enter') { e.preventDefault(); ok(); }
      if (e.key === 'Escape') { e.preventDefault(); closePopup(); }
    });
    p.append(h('div', { class: 'p-head' }, opts.title),
      h('div', { class: 'p-body' }, opts.label ? h('div', { class: 'hint' }, opts.label) : null, input, error),
      h('div', { class: 'p-foot' }, h('button', { onclick: closePopup }, 'Cancel'), h('button', { class: 'primary', onclick: ok }, opts.okLabel || 'OK')));
    setTimeout(() => { input.focus(); if (opts.selectStem) { const i = input.value.lastIndexOf('/') + 1; input.setSelectionRange(i, input.value.length - 4); } }, 0);
  });
}
function pickTask(owner) {
  const globalOwner = owner === root();
  const items = Object.keys(schema.tasks).map((cls) => {
    const def = schema.tasks[cls];
    return { value: cls, label: cls, sub: def.description, group: def.global ? 'Global tasks (data providers)' : 'Tasks', global: def.global };
  });
  items.sort((a, b) => (a.global === globalOwner ? 0 : 1) - (b.global === globalOwner ? 0 : 1) || a.label.localeCompare(b.label));
  openPicker({ title: globalOwner ? 'Add global task' : 'Add task to ' + attr(owner, 'name'), items, onPick: (it) => opAddTask(owner, it.value) });
}
function pickConsideration(state) {
  openPicker({ title: 'Add consideration', items: Object.keys(schema.considerations).map((cls) => ({ value: cls, label: cls, sub: schema.considerations[cls].description })),
    onPick: (it) => opAddConsideration(state, it.value) });
}
function showHelp() {
  const rows = [
    ['Ctrl+S', 'save'], ['Ctrl+Z / Ctrl+Y', 'undo / redo (Ctrl+Shift+Z redo)'], ['Ctrl+P', 'filter files'], ['Ctrl+F', 'find in the outline'],
    ['Ctrl+Shift+X', 'XML source view'], ['↑ ↓', 'previous / next row'], ['← →', 'collapse / expand, parent'],
    ['Alt+↑ / Alt+↓', 'move the selection up / down'], ['Ins', 'add a child state'], ['Shift+Ins', 'add a sibling state'],
    ['T', 'add a task to the state'], ['Ctrl+D', 'duplicate'], ['Del', 'delete'], ['Ctrl+E', 'enable / disable'],
    ['I', 'invert the condition'], ['F2', 'rename the state'], ['Esc', 'close menus, select the parent'],
    ['drag & drop', 'states: before / into / after a state; tasks: onto a state or another task'], ['?selftest=1', 'round trip test of every file'],
  ];
  openPopup((p) => {
    p.append(h('div', { class: 'p-head' }, 'Keyboard shortcuts'),
      h('div', { class: 'p-body help' }, h('table', null, rows.map(([k, d]) => h('tr', null, h('td', null, h('kbd', null, k)), h('td', null, d))))),
      h('div', { class: 'p-foot' }, h('button', { onclick: closePopup }, 'Close')));
  });
}

// ---------- details: field builders

function field(label, control, opts) {
  opts = opts || {};
  return h('div', { class: 'field' + (opts.wide ? ' wide' : ''), title: opts.help || '' },
    label !== null ? h('label', { class: opts.required ? 'required' : '' }, label) : null,
    h('div', { class: 'ctl' }, control));
}
function datalist(values) {
  const id = 'dl' + (++uid);
  return { id, el: h('datalist', { id }, (values || []).map((v) => h('option', { value: v }))) };
}
function textInput(el, name, opts) {
  opts = opts || {};
  const input = h('input', { type: 'text', value: attr(el, name), placeholder: opts.placeholder || '', spellcheck: false,
    class: opts.mono ? 'mono' : '' });
  if (opts.focusKey) input.dataset.focus = opts.focusKey;
  const check = () => input.classList.toggle('invalid', !!(opts.check && input.value !== '' && !opts.check(input.value)));
  check();
  input.addEventListener('input', () => {
    if (opts.keepEmpty && input.value === '') setAttr(el, name, '');
    else setOrRemove(el, name, input.value);
    check();
    touch();
    if (opts.onInput) opts.onInput(input.value);
  });
  input.addEventListener('change', () => { if (opts.onCommit) opts.onCommit(input.value); commit(); if (opts.rerender) renderDetails(); });
  if (!opts.list) return input;
  const dl = datalist(opts.list);
  input.setAttribute('list', dl.id);
  return [input, dl.el];
}
function selectInput(el, name, values, opts) {
  opts = opts || {};
  const s = h('select');
  if (opts.none !== false) s.append(h('option', { value: '' }, opts.noneLabel || (opts.def !== undefined ? '(default: ' + opts.def + ')' : '(none)')));
  const current = attr(el, name);
  let found = current === '';
  for (const v of values) {
    const value = typeof v === 'string' ? v : v.value;
    const label = typeof v === 'string' ? v : v.label;
    s.append(h('option', { value }, label));
    if (value === current) found = true;
  }
  if (!found) s.append(h('option', { value: current }, current + '  (unknown)'));
  s.value = current;
  s.addEventListener('change', () => {
    setOrRemove(el, name, s.value);
    if (opts.onChange) opts.onChange(s.value);
    commit();
    renderAll();
  });
  return s;
}
function boolInput(el, name, def) {
  return selectInput(el, name, BOOLS, { def });
}
function commentInput(el) {
  const ta = h('textarea', { rows: Math.min(8, Math.max(2, getComment(el).split('\n').length + 1)), placeholder: 'developer comment', spellcheck: true });
  ta.value = getComment(el);
  ta.addEventListener('input', () => { setComment(el, ta.value); touch(); });
  ta.addEventListener('change', () => commit());
  return ta;
}
function pathInput(el, name, groups, opts) {
  opts = opts || {};
  const wrap = h('span', { class: 'row grow', style: 'margin:0;flex:1' });
  const current = attr(el, name);
  const build = (asText) => {
    wrap.textContent = '';
    if (asText) {
      const all = [];
      for (const g of groups) for (const it of g.items) all.push(it.value);
      wrap.append(...textInput(el, name, { list: all, mono: true, rerender: true, keepEmpty: opts.keepEmpty }));
    } else {
      const s = h('select', { class: 'grow mono' });
      s.append(h('option', { value: '' }, opts.noneLabel || '(none)'));
      let found = current === '';
      for (const g of groups) {
        const og = h('optgroup', { label: g.label });
        for (const it of g.items) {
          og.append(h('option', { value: it.value, title: it.value }, it.label + (it.type ? '  : ' + it.type : '')));
          if (it.value === current) found = true;
        }
        s.append(og);
      }
      if (!found) s.append(h('option', { value: current }, '⚠ ' + current));
      s.value = current;
      s.addEventListener('change', () => {
        if (s.value === '' && opts.keepEmpty) setAttr(el, name, ''); else setOrRemove(el, name, s.value);
        if (opts.onChange) opts.onChange(s.value);
        commit();
        renderAll();
      });
      wrap.append(s);
    }
    wrap.append(h('button', { class: 'icon', title: asText ? 'pick from the list' : 'edit as text', onclick: () => build(!asText) }, asText ? '▾' : '✎'));
  };
  build(false);
  return wrap;
}
function group(title, body, opts) {
  opts = opts || {};
  const key = opts.key || title;
  const g = h('div', { class: 'group' + (collapsedGroups.has(key) ? ' collapsed' : '') });
  g.append(h('div', { class: 'g-head', onclick: () => {
    g.classList.toggle('collapsed');
    if (g.classList.contains('collapsed')) collapsedGroups.add(key); else collapsedGroups.delete(key);
    storageSet('ste.groups', Array.from(collapsedGroups).join('|'));
  } }, h('span', { class: 'twist' }, '▾'), title, opts.count !== undefined ? h('span', { class: 'count' }, '(' + opts.count + ')') : null,
  h('span', { class: 'spacer' }), opts.actions || null), h('div', { class: 'g-body' }, body));
  if (opts.actions) for (const b of g.querySelectorAll('.g-head button')) b.addEventListener('click', (e) => e.stopPropagation());
  return g;
}
function otherAttributes(el, known) {
  const body = h('div');
  const list = Array.from(el.attributes).filter((a) => !known.includes(a.name));
  for (const a of list) {
    body.append(h('div', { class: 'row' }, h('span', { class: 'attr-name', style: 'min-width:90px' }, a.name),
      h('span', { class: 'grow', style: 'display:flex' }, textInput(el, a.name, { keepEmpty: true })),
      h('button', { class: 'icon danger', title: 'remove the attribute', onclick: () => mutate(() => { el.removeAttribute(a.name); return el; }) }, '×')));
  }
  const nameIn = h('input', { type: 'text', placeholder: 'attribute', spellcheck: false, style: 'width:110px' });
  const valueIn = h('input', { type: 'text', placeholder: 'value', spellcheck: false, class: 'grow' });
  const add = () => {
    const n = nameIn.value.trim();
    if (!/^[A-Za-z_][\w.-]*$/.test(n)) { toast('not an attribute name', 'error'); return; }
    mutate(() => { setAttr(el, n, valueIn.value); return el; });
  };
  valueIn.addEventListener('keydown', (e) => { if (e.key === 'Enter') add(); });
  body.append(h('div', { class: 'row' }, nameIn, valueIn, h('button', { class: 'small', onclick: add }, 'Add')));
  return group('Other attributes', body, { count: list.length, key: 'other' });
}
function problemsFor(el) {
  const own = problems.filter((p) => p.el === el || (p.el && el.contains(p.el) && normalizeSel(p.el) === el));
  if (!own.length) return null;
  return h('div', { class: 'group' }, h('div', { class: 'g-body' }, own.map((p) => h('div', { class: 'problem ' + p.sev, style: 'grid-template-columns:22px 1fr;border:none;padding:1px 0', onclick: () => select(p.el) },
    h('span', { class: 'sev' }, p.sev === 'error' ? '✖' : p.sev === 'warning' ? '⚠' : 'i'), h('span', null, p.msg)))));
}
function crumbs(el) {
  const parts = [];
  for (let n = el.parentNode; n && n.nodeType === 1; n = n.parentNode) {
    if (n === root()) { parts.unshift({ el: n, label: cur.path.replace(/^.*\//, '') }); break; }
    if (n.tagName === 'State') parts.unshift({ el: n, label: attr(n, 'name') });
    else if (n.tagName === 'Task') parts.unshift({ el: n, label: shortClass(attr(n, 'class')) });
    else if (n.tagName === 'Transition') parts.unshift({ el: n, label: 'Transition' });
    else if (n.tagName === 'Any' || n.tagName === 'All') parts.unshift({ el: n, label: n.tagName });
    else if (n.tagName === 'GlobalTasks') parts.unshift({ el: root(), label: 'Global Tasks' });
  }
  const out = h('div', { class: 'crumbs' });
  parts.forEach((p, i) => { if (i) out.append(' › '); out.append(h('a', { onclick: () => select(p.el) }, p.label)); });
  return out;
}
function title(kind, what, actions) {
  return h('div', { class: 'd-title' }, h('span', { class: 'kind' }, kind), h('span', { class: 'what', title: what }, what), h('span', { class: 'spacer' }), actions || null);
}
function itemButtons(el, extra) {
  return [extra || null,
    h('button', { class: 'icon', title: 'move up', onclick: (e) => { e.stopPropagation(); opMove(el, -1); } }, '↑'),
    h('button', { class: 'icon', title: 'move down', onclick: (e) => { e.stopPropagation(); opMove(el, 1); } }, '↓'),
    h('button', { class: 'icon danger', title: 'delete', onclick: (e) => { e.stopPropagation(); opDelete(el); } }, '×')];
}

// ---------- details panel

function renderDetails() {
  const box = $('#details');
  const scroll = box.scrollTop;
  const keepScroll = box.dataset.for === String(locate(sel));
  box.textContent = '';
  box.dataset.for = String(locate(sel));
  if (!cur || !cur.doc) { box.append(h('div', { class: 'empty-hint' }, cur ? 'Fix the XML in the source view.' : 'Nothing open.')); return; }
  if (!sel || !D.documentElement.contains(sel)) sel = root();
  const kind = kindOf(sel);
  const ctx = { props: memberGroups('property'), delegates: null };
  const parts = [];
  if (kind === 'tree') parts.push(detailsTree(sel));
  else if (kind === 'state') parts.push(detailsState(sel, ctx));
  else if (kind === 'task' || kind === 'globalTask') parts.push(detailsTask(sel, ctx));
  else if (kind === 'transition') parts.push(detailsTransition(sel, ctx));
  else if (kind === 'condition' || kind === 'group') parts.push(detailsCondition(sel, ctx));
  else if (kind === 'consideration') parts.push(detailsConsideration(sel, ctx));
  box.append(...parts.flat());
  if (keepScroll) box.scrollTop = scroll;
}
function detailsTree(r) {
  const out = [title('State tree', cur.path)];
  const p = problemsFor(r);
  if (p) out.push(p);
  out.push(field('Comment', commentInput(r)));
  out.push(field('version', h('span', { class: 'mono' }, attr(r, 'version'))));
  const states = allStates();
  const tasks = states.reduce((n, s) => n + tasksOf(s).length, 0);
  out.push(h('div', { class: 'hint' }, plural(states.length, 'state') + ', ' + plural(tasks, 'state task') + ', ' + plural(globalTasks().length, 'global task')));
  const gl = h('div');
  for (const t of globalTasks()) {
    gl.append(h('div', { class: 'item' }, h('div', { class: 'item-head' },
      h('span', { class: 'title', onclick: () => select(t) }, attr(t, 'class') + (has(t, 'name') ? ' [' + attr(t, 'name') + ']' : '')), itemButtons(t))));
  }
  gl.append(h('div', { class: 'toolbar-row' }, h('button', { class: 'small', onclick: () => pickTask(r) }, '+ Global Task')));
  out.push(group('Global Tasks', gl, { count: globalTasks().length }));
  const st = h('div');
  for (const s of childStates(r)) {
    st.append(h('div', { class: 'item' }, h('div', { class: 'item-head' }, h('span', { class: 'title', onclick: () => select(s) }, attr(s, 'name')), itemButtons(s))));
  }
  st.append(h('div', { class: 'toolbar-row' }, h('button', { class: 'small', onclick: () => opAddState(r) }, '+ State')));
  out.push(group('Top level states', st, { count: childStates(r).length }));
  out.push(otherAttributes(r, ['version']));
  return out;
}
function detailsState(s, ctx) {
  const out = [];
  const type = attr(s, 'type', 'State');
  const children = childStates(s);
  out.push(title('State', attr(s, 'name'), h('span', null,
    h('button', { class: 'icon', title: 'add child state (Ins)', onclick: () => opAddState(s) }, '+ Child'),
    h('button', { class: 'icon', title: 'duplicate (Ctrl+D)', onclick: () => opDuplicate(s) }, 'Dup'),
    h('button', { class: 'icon danger', title: 'delete (Del)', onclick: () => opDelete(s) }, 'Del'))));
  out.push(crumbs(s));
  const p = problemsFor(s);
  if (p) out.push(p);
  let oldName = attr(s, 'name');
  const nameInput = textInput(s, 'name', { focusKey: 'name', onCommit: (value) => {
    if (oldName && value && value !== oldName) {
      renameStateRefs(root(), new Map([[oldName, value]]));
      if (cur.collapsed.delete(oldName)) cur.collapsed.add(value);
      renderOutline();
    }
    oldName = value;
  }, onInput: (value) => { const w = $('#details .what'); if (w) w.textContent = value; } });
  out.push(field('name', nameInput, { required: true, help: 'unique in the tree; renaming updates the paths, targets and linkedSubtree that use it' }));
  out.push(field('type', selectInput(s, 'type', STATE_TYPES, { def: 'State' }), { help: 'State; Subtree: entered only through a Linked state or a transition; Linked: runs the Subtree state named by linkedSubtree' }));
  if (type === 'Linked' || has(s, 'linkedSubtree')) {
    const subtrees = allStates().filter((x) => attr(x, 'type') === 'Subtree').map((x) => attr(x, 'name'));
    out.push(field('linkedSubtree', selectInput(s, 'linkedSubtree', subtrees, { noneLabel: '(choose a Subtree state)' }), { required: type === 'Linked' }));
  }
  out.push(field('selection', selectInput(s, 'selection', SELECTIONS, { def: 'TrySelectChildrenInOrder' }), { help: 'how a child is selected when the state is entered' }));
  out.push(field('weight', textInput(s, 'weight', { placeholder: '1', check: isNum }), { help: 'utility weight: weight x product of the considerations' }));
  out.push(field('retryDelay', textInput(s, 'retryDelay', { placeholder: children.length ? '0' : '3000', check: isUInt }), { help: 'ms a failed state is not selected again' }));
  out.push(field('enabled', boolInput(s, 'enabled', 'true')));
  out.push(field('Comment', commentInput(s)));

  const ec = kid(s, 'EnterConditions');
  const ecBody = h('div');
  if (ec) ecBody.append(field('match', selectInput(ec, 'match', ['all', 'any'], { def: 'all' })));
  ecBody.append(conditionList(ec, s, ctx));
  out.push(group('Enter Conditions', ecBody, { count: ec ? kids(ec, CONDITION_TAGS).length : 0, key: 'enter' }));

  const cons = kids(kid(s, 'Considerations'), 'Consideration');
  const consBody = h('div');
  for (const c of cons) consBody.append(considerationItem(c, ctx));
  consBody.append(h('div', { class: 'toolbar-row' }, h('button', { class: 'small', onclick: () => pickConsideration(s) }, '+ Consideration')));
  out.push(group('Considerations (utility)', consBody, { count: cons.length, key: 'cons' }));

  const tasks = tasksOf(s);
  const tBody = h('div');
  for (const t of tasks) {
    tBody.append(h('div', { class: 'item' + (attr(t, 'enabled') === 'false' ? ' disabled' : '') }, h('div', { class: 'item-head' },
      h('span', { class: 'title', onclick: () => select(t), title: (taskDef(attr(t, 'class')) || {}).description || '' },
        attr(t, 'class') + (taskInfo(t) ? '  — ' + taskInfo(t) : '')), itemButtons(t))));
  }
  tBody.append(h('div', { class: 'toolbar-row' }, h('button', { class: 'small', onclick: () => pickTask(s) }, '+ Task')));
  out.push(group('Tasks', tBody, { count: tasks.length, key: 'tasks' }));

  const trs = kids(kid(s, 'Transitions'), 'Transition');
  const trBody = h('div');
  for (const tr of trs) {
    trBody.append(h('div', { class: 'item' }, h('div', { class: 'item-head' },
      h('span', { class: 'title', onclick: () => select(tr) }, transLabel(tr) + ' → ' + targetLabel(attr(tr, 'target'))), itemButtons(tr))));
  }
  trBody.append(h('div', { class: 'toolbar-row' }, h('button', { class: 'small', onclick: () => opAddTransition(s) }, '+ Transition')));
  out.push(group('Transitions', trBody, { count: trs.length, key: 'transitions' }));

  const cBody = h('div');
  for (const c of children) {
    cBody.append(h('div', { class: 'item' }, h('div', { class: 'item-head' }, h('span', { class: 'title', onclick: () => select(c) }, attr(c, 'name')), itemButtons(c))));
  }
  cBody.append(h('div', { class: 'toolbar-row' }, h('button', { class: 'small', onclick: () => opAddState(s) }, '+ Child State')));
  out.push(group('Child states', cBody, { count: children.length, key: 'children' }));
  out.push(otherAttributes(s, KNOWN_ATTRS.State));
  return out;
}
function detailsTask(t, ctx) {
  const out = [];
  const global = isGlobalTask(t);
  const cls = attr(t, 'class');
  const def = taskDef(cls);
  out.push(title(global ? 'Global task' : 'Task', cls + (has(t, 'name') ? ' [' + attr(t, 'name') + ']' : ''), h('span', null,
    h('button', { class: 'icon', title: 'duplicate (Ctrl+D)', onclick: () => opDuplicate(t) }, 'Dup'),
    h('button', { class: 'icon danger', title: 'delete (Del)', onclick: () => opDelete(t) }, 'Del'))));
  out.push(crumbs(t));
  const p = problemsFor(t);
  if (p) out.push(p);
  if (def && def.doc) out.push(docHtml(def.doc));
  const classes = Object.keys(schema.tasks).map((c) => ({ value: c, label: c + (schema.tasks[c].global ? '  (global)' : '') }));
  out.push(field('class', selectInput(t, 'class', classes, { none: false }), { required: true }));
  out.push(field('name', textInput(t, 'name', { placeholder: 'picks the task in paths: Class[name]' })));
  out.push(field('completion', selectInput(t, 'completion', COMPLETIONS, { def: global ? 'IgnoreResult' : 'AlwaysFinish' }), { help: 'how the task finishing completes its state' }));
  out.push(field('show_log', boolInput(t, 'show_log', 'task default')));
  out.push(field('enabled', boolInput(t, 'enabled', 'true')));
  out.push(field('Comment', commentInput(t)));

  const taskAttrs = def ? def.taskAttributes || {} : {};
  if (Object.keys(taskAttrs).length) {
    const body = h('div');
    for (const name of Object.keys(taskAttrs)) {
      const a = taskAttrs[name];
      const values = (a.options || []).concat((a.examples || []).filter((v) => !(a.options || []).includes(v)));
      body.append(field(name, textInput(t, name, { list: values, placeholder: (a.options || []).join(' | ') })));
    }
    out.push(group('Task attributes', body, { key: 'taskattrs' }));
  }

  const bindings = kids(t, 'Binding');
  const bBody = h('div');
  const props = def ? def.properties : [];
  const others = memberGroups('property', t);
  for (const b of bindings) {
    const lp = propOf(def, attr(b, 'local'));
    const groups = others.map((g) => ({ label: g.label, items: g.items.filter((it) => !lp || canonType(it.type) === canonType(lp.type)) })).filter((g) => g.items.length);
    const localSel = selectInput(b, 'local', props.map((x) => ({ value: x.name, label: x.name + '  : ' + x.type })), { noneLabel: '(property)' });
    bBody.append(h('div', { class: 'item' }, h('div', { class: 'item-head' }, h('span', { class: 'grow', style: 'display:flex;flex:1' }, localSel), h('span', { class: 'cond-op' }, '←'), itemButtons(b)),
      h('div', { class: 'row' }, pathInput(b, 'external', groups, { noneLabel: '(external property)' }))));
  }
  const unbound = props.filter((x) => !bindings.some((b) => attr(b, 'local') === x.name));
  bBody.append(h('div', { class: 'toolbar-row' }, h('button', { class: 'small', disabled: !def, onclick: () => mutate(() => {
    insertTaskChild(t, mk('Binding', { local: unbound.length ? unbound[0].name : (props[0] ? props[0].name : ''), external: '' }));
    return t;
  }) }, '+ Binding')));
  if (props.length) bBody.append(h('div', { class: 'hint' }, 'properties: ' + props.map((x) => x.name + ' : ' + x.type).join(', ')));
  out.push(group('Bindings', bBody, { count: bindings.length, key: 'bindings' }));

  out.push(settingsGroup(t, def));

  if (global || kid(t, 'Conditions')) {
    const c = kid(t, 'Conditions');
    const body = h('div');
    if (!global) body.append(h('div', { class: 'raw-error' }, '<Conditions> are only allowed on global tasks'));
    else body.append(h('div', { class: 'hint' }, 'tested before every update of the task; set its bActive'));
    if (c) body.append(field('match', selectInput(c, 'match', ['all', 'any'], { def: 'all' })));
    body.append(conditionList(c, t, ctx));
    out.push(group('Conditions', body, { count: c ? kids(c, CONDITION_TAGS).length : 0, key: 'taskconds' }));
  }
  if (def && (def.properties.length || def.delegates.length)) {
    const list = h('div', { class: 'prop-list' });
    for (const x of def.properties) list.append(h('div', { title: (x.traits || []).join(', ') }, h('b', null, x.name), ' : ' + x.type));
    for (const d of def.delegates) list.append(h('div', null, h('b', null, d), ' : delegate'));
    out.push(group('Exposes', list, { key: 'exposes', count: def.properties.length + def.delegates.length }));
  }
  out.push(otherAttributes(t, KNOWN_ATTRS.Task.concat(Object.keys(taskAttrs))));
  return out;
}
function settingsGroup(t, def) {
  const body = h('div');
  const known = def ? def.settings || {} : {};
  if (def && def.settingsHelp) body.append(docHtml(def.settingsHelp));
  const tags = Object.keys(known);
  for (const n of settingsNodes(t)) if (n.nodeType === 1 && !tags.includes(n.tagName)) tags.push(n.tagName);
  for (const tag of tags) {
    const spec = known[tag] || { attributes: {}, repeat: true };
    const instances = kids(t, tag);
    for (const inst of instances) {
      const names = Object.keys(spec.attributes || {});
      for (const a of inst.attributes) if (!names.includes(a.name)) names.push(a.name);
      const row = h('div', { class: 'row' }, h('span', { class: 'tag' }, '<' + tag + '>'));
      for (const name of names) {
        const a = (spec.attributes || {})[name] || {};
        const values = (a.options || []).concat((a.examples || []).filter((v) => !(a.options || []).includes(v)));
        const input = textInput(inst, name, { list: values, placeholder: name, onInput: () => { if (document.activeElement !== raw) raw.value = settingsText(t); } });
        row.append(h('span', { class: 'attr-name' }, name), ...[].concat(input).map((x) => { if (x.tagName === 'INPUT') x.style.width = '90px'; x.classList.add('grow'); return x; }));
      }
      row.append(h('button', { class: 'icon danger', title: 'remove', onclick: () => mutate(() => { inst.remove(); return t; }) }, '×'));
      body.append(row);
    }
    if (!instances.length || spec.repeat) {
      body.append(h('div', { class: 'toolbar-row' }, h('button', { class: 'small', onclick: () => mutate(() => {
        const el = mk(tag);
        for (const name of Object.keys(spec.attributes || {})) {
          const a = spec.attributes[name];
          el.setAttribute(name, (a.examples && a.examples[0]) || (a.options && a.options[0]) || '');
          break;
        }
        insertTaskChild(t, el);
        return t;
      }) }, '+ ' + tag)));
    }
  }
  const raw = h('textarea', { class: 'raw', rows: Math.min(14, Math.max(3, settingsText(t).split('\n').length + 1)), spellcheck: false, wrap: 'off' });
  raw.value = settingsText(t);
  const error = h('div', { class: 'raw-error' });
  const apply = () => {
    if (raw.value === settingsText(t)) return;
    let message = null;
    invalidate();
    message = applySettingsText(t, raw.value);
    if (message) { error.textContent = message; return; }
    error.textContent = '';
    commit();
    renderAll();
  };
  raw.addEventListener('keydown', (e) => { if (e.key === 'Enter' && (e.ctrlKey || e.metaKey)) { e.preventDefault(); apply(); } });
  raw.addEventListener('change', apply);
  body.append(h('div', { class: 'hint' }, 'settings XML (every child but Comment / Binding / Conditions; XML comments are kept), applied on blur or Ctrl+Enter'), raw, error);
  return group('Settings', body, { count: settingsNodes(t).filter((n) => n.nodeType === 1).length, key: 'settings' });
}
function detailsTransition(tr, ctx) {
  const out = [];
  const st = ownerState(tr);
  out.push(title('Transition', transLabel(tr) + ' → ' + targetLabel(attr(tr, 'target')), h('span', null,
    h('button', { class: 'icon', title: 'duplicate (Ctrl+D)', onclick: () => opDuplicate(tr) }, 'Dup'),
    h('button', { class: 'icon danger', title: 'delete (Del)', onclick: () => opDelete(tr) }, 'Del'))));
  out.push(crumbs(tr));
  const p = problemsFor(tr);
  if (p) out.push(p);
  const trigger = attr(tr, 'trigger');
  out.push(field('trigger', selectInput(tr, 'trigger', TRIGGERS, { noneLabel: '(choose)' }), { required: true }));
  const targets = [{ label: 'Special', items: SPECIAL_TARGETS.map((x) => ({ value: x, label: x })) },
    { label: 'States', items: allStates().map((x) => ({ value: 'Root|' + attr(x, 'name'), label: '  '.repeat(statePath(x).split('|').length - 2) + attr(x, 'name') + (attr(x, 'type') === 'Subtree' ? '  (Subtree)' : '') })) }];
  out.push(field('target', pathInput(tr, 'target', targets, { noneLabel: '(choose)' }), { required: true }));
  if (trigger === 'OnDelegate' || has(tr, 'source')) {
    out.push(field('source', pathInput(tr, 'source', memberGroups('delegate'), { noneLabel: '(delegate)' }), { required: trigger === 'OnDelegate' }));
  }
  if (trigger === 'OnEvent' || has(tr, 'event')) out.push(field('event', textInput(tr, 'event', { placeholder: 'event tag' }), { required: trigger === 'OnEvent' }));
  out.push(field('priority', selectInput(tr, 'priority', PRIORITIES, { def: 'Normal' })));
  out.push(field('delay', textInput(tr, 'delay', { placeholder: '0', check: isUInt }), { help: 'ms after the trigger, while the state stays active' }));
  out.push(field('enabled', boolInput(tr, 'enabled', 'true')));
  out.push(field('Comment', commentInput(tr)));
  const c = kid(tr, 'Conditions');
  const body = h('div');
  if (c) body.append(field('match', selectInput(c, 'match', ['all', 'any'], { def: 'all' })));
  body.append(conditionList(c, tr, ctx));
  out.push(group('Conditions', body, { count: c ? kids(c, CONDITION_TAGS).length : 0, key: 'trconds' }));
  out.push(otherAttributes(tr, KNOWN_ATTRS.Transition));
  if (st) out.push(h('div', { class: 'hint' }, 'of state ', h('a', { class: 'link', onclick: () => select(st) }, attr(st, 'name'))));
  return out;
}
function conditionList(container, owner, ctx) {
  const body = h('div');
  if (container) for (const c of kids(container, CONDITION_TAGS)) body.append(conditionEntry(c, ctx));
  body.append(h('div', { class: 'toolbar-row' },
    h('button', { class: 'small', onclick: () => opAddCondition(owner) }, '+ Condition'),
    h('button', { class: 'small', onclick: () => opAddGroup(owner, 'Any') }, '+ Any'),
    h('button', { class: 'small', onclick: () => opAddGroup(owner, 'All') }, '+ All')));
  return body;
}
function conditionEntry(c, ctx) {
  if (c.tagName === 'Condition') return conditionItem(c, ctx, false);
  const g = h('div', { class: 'cond-group' + (c.tagName === 'Any' ? ' any' : '') + (sel === c ? ' item selected' : '') });
  const tagSel = h('select', null, h('option', { value: 'Any' }, 'Any (one passes)'), h('option', { value: 'All' }, 'All (every one passes)'));
  tagSel.value = c.tagName;
  tagSel.addEventListener('change', () => mutate(() => {
    const ng = mk(tagSel.value);
    while (c.firstChild) ng.appendChild(c.firstChild);
    for (const a of Array.from(c.attributes)) ng.setAttribute(a.name, a.value);
    c.parentNode.replaceChild(ng, c);
    return sel === c ? ng : sel;
  }));
  g.append(h('div', { class: 'item-head' }, tagSel, h('span', { class: 'spacer' }),
    itemButtons(c, [h('button', { class: 'icon', title: 'add a condition to the group', onclick: () => opAddCondition(c) }, '+'),
      h('button', { class: 'icon', title: 'unwrap the group', onclick: () => opUnwrap(c) }, '⤴')])));
  for (const e of kids(c, CONDITION_TAGS)) g.append(conditionEntry(e, ctx));
  return g;
}
function conditionFields(c, ctx, wrap) {
  const cls = attr(c, 'class');
  const def = schema.conditions[cls];
  const out = [];
  if (cls === 'Condition_Compare') {
    const groups = ctx.props;
    out.push(h('div', { class: 'row' }, pathInput(c, 'binding', groups, { noneLabel: '(property)', keepEmpty: true })));
    const compareSel = selectInput(c, 'compare', COMPARES, { noneLabel: '(is set)' });
    const valueIn = textInput(c, 'value', { placeholder: 'value' });
    const keyIn = textInput(c, 'key', { placeholder: 'key', check: isNum });
    out.push(h('div', { class: 'row' }, compareSel, [].concat(valueIn).map((x) => { x.classList.add('grow'); return x; }),
      h('span', { class: 'attr-name' }, 'key'), [].concat(keyIn).map((x) => { x.style.width = '70px'; return x; })));
  } else if (def) {
    for (const a of def.attributes || []) {
      let control;
      if (a.kind === 'enum') control = selectInput(c, a.name, a.options || [], { def: a.default });
      else if (a.kind === 'flags') {
        control = h('span', { class: 'row', style: 'margin:0' });
        const currentValue = () => attr(c, a.name, a.default || '').split(',').map((s) => s.trim()).filter(Boolean);
        for (const o of a.options || []) {
          const box = h('input', { type: 'checkbox', checked: currentValue().includes(o) });
          box.addEventListener('change', () => mutate(() => {
            const v = currentValue().filter((x) => x !== o);
            if (box.checked) v.push(o);
            const ordered = (a.options || []).filter((x) => v.includes(x)).join(',');
            if (ordered === (a.default || '')) c.removeAttribute(a.name); else setAttr(c, a.name, ordered);
            return sel;
          }));
          control.append(h('label', { class: 'check' }, box, o));
        }
      } else if (a.kind === 'property') control = pathInput(c, a.name, ctx.props, { noneLabel: '(property)', keepEmpty: true });
      else control = textInput(c, a.name, { placeholder: a.default || '', check: a.kind === 'number' ? isNum : null });
      out.push(field(a.name, control, { required: a.required, help: a.help }));
    }
  }
  const known = ['class', 'inverted'].concat(def ? (def.attributes || []).map((a) => a.name) : []);
  const extra = Array.from(c.attributes).filter((a) => !known.includes(a.name));
  for (const a of extra) {
    out.push(h('div', { class: 'row' }, h('span', { class: 'attr-name', style: 'min-width:70px' }, a.name),
      h('span', { class: 'grow', style: 'display:flex' }, textInput(c, a.name, { keepEmpty: true })),
      h('button', { class: 'icon danger', title: 'remove the attribute', onclick: () => mutate(() => { c.removeAttribute(a.name); return sel; }) }, '×')));
  }
  if (wrap) return out;
  return h('div', null, out);
}
function conditionItem(c, ctx, expanded) {
  const inverted = attr(c, 'inverted') === 'true';
  const classes = Object.keys(schema.conditions).map((x) => ({ value: x, label: x.replace(/^Condition_/, '') }));
  const classSel = selectInput(c, 'class', classes, { none: false });
  classSel.classList.add('grow');
  const notBtn = h('button', { class: 'small' + (inverted ? ' primary' : ''), title: 'inverted: negates the result', onclick: () => opInvert(c) }, 'NOT');
  const item = h('div', { class: 'item' + (sel === c && !expanded ? ' selected' : '') },
    h('div', { class: 'item-head' }, notBtn, classSel,
      expanded ? null : h('button', { class: 'icon', title: 'select (comment, details)', onclick: () => select(c) }, '…'),
      itemButtons(c, [h('button', { class: 'icon', title: 'wrap in Any', onclick: () => opWrap(c, 'Any') }, '∨'),
        h('button', { class: 'icon', title: 'wrap in All', onclick: () => opWrap(c, 'All') }, '∧')])),
    conditionFields(c, ctx));
  if (getComment(c) && !expanded) item.append(h('div', { class: 'hint' }, getComment(c)));
  return item;
}
function detailsCondition(c, ctx) {
  const out = [];
  const owner = ownerOf(c, ['State', 'Transition', 'Task']);
  if (c.tagName === 'Condition') {
    out.push(title('Condition', condLabel(c)));
    out.push(crumbs(c));
    const p = problemsFor(c);
    if (p) out.push(p);
    const def = schema.conditions[attr(c, 'class')];
    if (def && def.doc) out.push(docHtml(def.doc));
    out.push(conditionItem(c, ctx, true));
    out.push(field('Comment', commentInput(c)));
  } else {
    out.push(title(c.tagName + ' group', condLabel(c)));
    out.push(crumbs(c));
    const p = problemsFor(c);
    if (p) out.push(p);
    out.push(h('div', { class: 'hint' }, c.tagName === 'Any' ? 'passes when one of its entries passes' : 'passes when every entry passes'));
    out.push(conditionEntry(c, ctx));
  }
  if (owner) out.push(h('div', { class: 'toolbar-row' }, h('button', { class: 'small', onclick: () => select(owner) }, '↑ ' + (owner.tagName === 'State' ? attr(owner, 'name') : owner.tagName))));
  return out;
}
function considerationItem(c, ctx) {
  const cls = attr(c, 'class');
  const def = schema.considerations[cls];
  const classSel = selectInput(c, 'class', Object.keys(schema.considerations).map((x) => ({ value: x, label: x.replace(/^Consideration_/, '') })), { none: false });
  classSel.classList.add('grow');
  const item = h('div', { class: 'item' + (sel === c ? ' selected' : '') }, h('div', { class: 'item-head' }, classSel,
    h('button', { class: 'icon', title: 'select', onclick: () => select(c) }, '…'), itemButtons(c)));
  if (def) {
    for (const a of def.attributes || []) {
      const numberProps = ctx.props.map((g) => ({ label: g.label, items: g.items.filter((it) => !a.number || it.traits.includes('number')) })).filter((g) => g.items.length);
      const control = a.kind === 'property' ? pathInput(c, a.name, numberProps, { noneLabel: '(number property)', keepEmpty: true })
        : textInput(c, a.name, { placeholder: a.default || '', check: isNum });
      item.append(field(a.name, control, { required: a.required, help: a.help }));
    }
  }
  if (def && def.children && def.children.Key) {
    const keys = kids(c, 'Key');
    const kb = h('div');
    let plot = curveSvg(c);
    const redraw = () => { const next = curveSvg(c); plot.replaceWith(next); plot = next; };
    for (const k of keys) {
      kb.append(h('div', { class: 'row' }, h('span', { class: 'attr-name' }, 'x'), [].concat(textInput(k, 'x', { check: isNum, onInput: redraw })).map((x) => { x.style.width = '70px'; return x; }),
        h('span', { class: 'attr-name' }, 'y'), [].concat(textInput(k, 'y', { check: isNum, onInput: redraw })).map((x) => { x.style.width = '70px'; return x; }),
        h('span', { class: 'spacer' }), h('button', { class: 'icon danger', title: 'remove the key', onclick: () => mutate(() => { k.remove(); return c; }) }, '×')));
    }
    kb.append(h('div', { class: 'toolbar-row' }, h('button', { class: 'small', onclick: () => mutate(() => {
      const last = keys[keys.length - 1];
      const x = last ? Math.min(1, Number(attr(last, 'x')) + 0.25) : 0;
      c.appendChild(mk('Key', { x: String(Number.isFinite(x) ? x : 0), y: last ? attr(last, 'y') : '0' }));
      return c;
    }) }, '+ Key')));
    kb.append(plot);
    item.append(h('div', { class: 'hint' }, 'curve over the normalized input (no keys: the input itself)'), kb);
  }
  return item;
}
function curveSvg(c) {
  const ns = 'http://www.w3.org/2000/svg';
  const W = 240;
  const H = 120;
  const P = 16;
  const sx = (x) => P + Math.max(0, Math.min(1, x)) * (W - 2 * P);
  const sy = (y) => H - P - Math.max(0, Math.min(1, y)) * (H - 2 * P);
  const svg = document.createElementNS(ns, 'svg');
  svg.setAttribute('width', W);
  svg.setAttribute('height', H);
  svg.setAttribute('class', 'curve');
  const add = (tag, attrs, text) => {
    const e = document.createElementNS(ns, tag);
    for (const k of Object.keys(attrs)) e.setAttribute(k, attrs[k]);
    if (text) e.textContent = text;
    svg.appendChild(e);
    return e;
  };
  for (const v of [0, 0.5, 1]) {
    add('line', { x1: sx(v), y1: sy(0), x2: sx(v), y2: sy(1), class: 'grid' });
    add('line', { x1: sx(0), y1: sy(v), x2: sx(1), y2: sy(v), class: 'grid' });
  }
  add('text', { x: 2, y: sy(1) + 3 }, '1');
  add('text', { x: 2, y: sy(0) + 3 }, '0');
  add('text', { x: sx(1) - 4, y: H - 3 }, '1');
  add('polyline', { points: sx(0) + ',' + sy(0) + ' ' + sx(1) + ',' + sy(1), class: 'ident' });
  const keys = kids(c, 'Key').map((k) => [Number(attr(k, 'x')), Number(attr(k, 'y'))]).filter((p) => p.every(Number.isFinite)).sort((a, b) => a[0] - b[0]);
  if (keys.length) {
    const pts = [];
    if (keys[0][0] > 0) pts.push([0, keys[0][1]]);
    for (const k of keys) pts.push(k);
    if (keys[keys.length - 1][0] < 1) pts.push([1, keys[keys.length - 1][1]]);
    add('polyline', { points: pts.map((p) => sx(p[0]) + ',' + sy(p[1])).join(' '), class: 'line' });
    for (const k of keys) add('circle', { cx: sx(k[0]), cy: sy(k[1]), r: 3, class: 'key' });
  } else add('polyline', { points: sx(0) + ',' + sy(0) + ' ' + sx(1) + ',' + sy(1), class: 'line' });
  return svg;
}
function detailsConsideration(c, ctx) {
  const out = [title('Consideration', attr(c, 'class'))];
  out.push(crumbs(c));
  const p = problemsFor(c);
  if (p) out.push(p);
  const def = schema.considerations[attr(c, 'class')];
  if (def && def.doc) out.push(docHtml(def.doc));
  out.push(considerationItem(c, ctx));
  out.push(field('Comment', commentInput(c)));
  const st = ownerState(c);
  if (st) out.push(h('div', { class: 'toolbar-row' }, h('button', { class: 'small', onclick: () => select(st) }, '↑ ' + attr(st, 'name'))));
  return out;
}

// ---------- problems panel

function renderProblems() {
  const box = $('#problems');
  box.textContent = '';
  const counts = { error: 0, warning: 0, info: 0 };
  for (const p of problems) counts[p.sev]++;
  const pc = $('#problemCounts');
  pc.textContent = '';
  pc.append(h('span', { class: 'e' }, '✖ ' + counts.error), h('span', { class: 'w' }, '⚠ ' + counts.warning), h('span', { class: 'i' }, 'i ' + counts.info));
  if (!cur) return;
  if (!cur.doc) { box.append(h('div', { class: 'problem error' }, h('span', { class: 'sev' }, '✖'), h('span', null, 'XML: ' + cur.parseError), h('span'))); return; }
  const showW = $('#showWarnings').checked;
  const showI = $('#showInfos').checked;
  const order = { error: 0, warning: 1, info: 2 };
  const shown = problems.filter((p) => p.sev === 'error' || (p.sev === 'warning' && showW) || (p.sev === 'info' && showI));
  shown.sort((a, b) => order[a.sev] - order[b.sev]);
  if (!shown.length) { box.append(h('div', { class: 'no-problems' }, problems.length ? 'only hidden problems' : '✔ No problems')); return; }
  for (const p of shown) {
    box.append(h('div', { class: 'problem ' + p.sev, onclick: () => select(p.el), title: p.msg },
      h('span', { class: 'sev' }, p.sev === 'error' ? '✖' : p.sev === 'warning' ? '⚠' : 'i'),
      h('span', null, p.msg), h('span', { class: 'where' }, p.el ? whereLabel(p.el) : '')));
  }
}

// ---------- files

async function api(url, opts) {
  const res = await fetch(url, opts);
  const type = res.headers.get('Content-Type') || '';
  const body = type.includes('json') ? await res.json() : await res.text();
  if (!res.ok) {
    const err = new Error(body && body.error ? body.error : res.status + ' ' + res.statusText);
    err.status = res.status;
    throw err;
  }
  return { body, res };
}
async function loadFiles() {
  const { body } = await api('/api/files');
  files = body.files;
  renderFileList();
}
function renderFileList() {
  const box = $('#fileList');
  const scroll = box.scrollTop;
  box.textContent = '';
  const q = $('#fileFilter').value.toLowerCase();
  const byFolder = new Map();
  for (const f of files) {
    if (q && !f.path.toLowerCase().includes(q)) continue;
    const i = f.path.lastIndexOf('/');
    const folder = i < 0 ? '' : f.path.slice(0, i);
    if (!byFolder.has(folder)) byFolder.set(folder, []);
    byFolder.get(folder).push(f);
  }
  const folders = Array.from(byFolder.keys()).sort((a, b) => (a === '' ? -1 : b === '' ? 1 : a.localeCompare(b)));
  for (const folder of folders) {
    const collapsed = !q && collapsedFolders.has(folder);
    const el = h('div', { class: 'folder' + (collapsed ? ' collapsed' : '') });
    el.append(h('div', { class: 'folder-head', onclick: () => { if (collapsedFolders.has(folder)) collapsedFolders.delete(folder); else collapsedFolders.add(folder); renderFileList(); } },
      h('span', { class: 'twist' }, collapsed ? '▸' : '▾'), folder || 'statetree', h('span', { class: 'count', style: 'font-weight:normal' }, ' ' + byFolder.get(folder).length)));
    const list = h('div', { class: 'folder-files' });
    for (const f of byFolder.get(folder)) {
      const s = sessions.get(f.path);
      list.append(h('div', { class: 'file' + (cur && cur.path === f.path ? ' active' : ''), title: f.path, onclick: () => openFile(f.path) },
        h('span', null, f.path.slice(f.path.lastIndexOf('/') + 1)), isDirty(s) ? h('span', { class: 'dot', title: 'modified' }, '●') : null,
        s && (s.problemCount || s.parseError) ? h('span', { class: 'err', title: 'errors' }, '✖' + (s.parseError ? '' : s.problemCount)) : null));
    }
    el.append(list);
    box.append(el);
  }
  if (!files.length) box.append(h('div', { class: 'empty-hint' }, 'No *.xml files under the root.'));
  box.scrollTop = scroll;
}
async function openFile(path, opts) {
  opts = opts || {};
  if (cur && cur.path === path && !opts.reload) return;
  if (cur) { cur.sel = sel; cur.scroll = $('#outline').scrollTop; }
  let s = sessions.get(path);
  if (!s || opts.reload) {
    try {
      const { body, res } = await api('/api/file?path=' + encodeURIComponent(path));
      const keep = s;
      s = newSession(path, body, res.headers.get('X-Mtime'));
      if (keep) s.collapsed = keep.collapsed;
      sessions.set(path, s);
    } catch (e) {
      toast('cannot open ' + path + ': ' + e.message, 'error');
      return;
    }
  }
  cur = s;
  D = s.doc;
  sel = s.sel && D && D.documentElement.contains(s.sel) ? s.sel : (D ? D.documentElement : null);
  invalidate();
  storageSet('ste.last', path);
  if (location.hash.slice(1) !== encodeURI(path)) history.replaceState(null, '', '#' + encodeURI(path));
  closeSource();
  if (!D) openSource();
  renderAll();
  $('#outline').scrollTop = s.scroll || 0;
}
async function saveFile() {
  if (!cur) return;
  if (sourceOpen && sourceDirty && !applySource()) return;
  if (!cur.doc) { toast('the XML does not parse: fix it first', 'error'); return; }
  commit();
  const text = cur.text;
  const send = async (force) => api('/api/file?path=' + encodeURIComponent(cur.path) + (force ? '&force=1' : ''), {
    method: 'PUT', headers: { 'Content-Type': 'text/plain; charset=utf-8', 'X-Base-Mtime': cur.mtime || '' }, body: text });
  try {
    let result;
    try {
      result = await send(false);
    } catch (e) {
      if (e.status !== 409 || !confirm(cur.path + ' changed on disk since it was loaded. Overwrite it?')) throw e;
      result = await send(true);
    }
    cur.savedText = text;
    cur.mtime = String(result.body.mtime);
    runValidate();
    const errors = problems.filter((p) => p.sev === 'error').length;
    toast('Saved ' + cur.path + (errors ? ' (' + plural(errors, 'error') + ')' : ''), errors ? 'error' : 'ok');
    refreshChrome();
    loadFiles().catch(() => {});
  } catch (e) {
    toast('Save failed: ' + e.message, 'error');
  }
}
async function revertFile() {
  if (!cur) return;
  if (isDirty(cur) && !confirm('Drop the changes of ' + cur.path + ' and reload it from disk?')) return;
  const path = cur.path;
  await openFile(path, { reload: true });
  toast('Reloaded ' + path);
}
function newFile() {
  const folder = cur && cur.path.includes('/') ? cur.path.slice(0, cur.path.lastIndexOf('/') + 1) : '';
  openPrompt({ title: 'New state tree', label: 'path under statetree/ (the server loads every STAI_*.xml)', value: folder + 'STAI_New.xml', okLabel: 'Create', selectStem: true,
    onOk: (path) => {
      path = path.replace(/\\/g, '/');
      if (!/^[\w ./-]+\.xml$/i.test(path) || path.split('/').some((p) => p === '..' || p === '.' || p === '')) return 'a relative *.xml path, e.g. Mage/STAI_Combat_New_Mage.xml';
      if (files.some((f) => f.path.toLowerCase() === path.toLowerCase())) return 'the file exists already';
      api('/api/new?path=' + encodeURIComponent(path), { method: 'POST', headers: { 'Content-Type': 'text/plain; charset=utf-8' }, body: TEMPLATE })
        .then(() => loadFiles()).then(() => { closePopup(); return openFile(path); })
        .then(() => { if (!/(^|\/)STAI_[^/]*$/.test(path)) toast('note: the server only loads STAI_*.xml files'); })
        .catch((e) => toast('Create failed: ' + e.message, 'error'));
      return null;
    } });
}

// ---------- source view

function fillSource() {
  if (!cur) return;
  $('#sourceText').value = cur.doc ? serialize(cur.doc) : cur.text;
  sourceDirty = false;
  const info = $('#sourceInfo');
  info.className = cur.doc ? '' : 'error';
  info.textContent = cur.doc ? 'XML of ' + cur.path + ' (as it will be saved)' : 'parse error: ' + cur.parseError;
}
function openSource() {
  if (!cur) return;
  sourceOpen = true;
  $('#outline').classList.add('hidden');
  $('#source').classList.remove('hidden');
  fillSource();
  $('#sourceText').focus();
}
function closeSource() {
  if (sourceOpen && sourceDirty && cur && cur.doc && !confirm('Drop the edits of the XML source?')) return false;
  sourceOpen = false;
  sourceDirty = false;
  $('#source').classList.add('hidden');
  $('#outline').classList.remove('hidden');
  return true;
}
function applySource() {
  const text = $('#sourceText').value.replace(/\r\n/g, '\n');
  const p = parseXml(text);
  const info = $('#sourceInfo');
  if (p.error) { info.className = 'error'; info.textContent = 'parse error: ' + p.error; return false; }
  const wasBroken = !cur.doc;
  cur.doc = D = p.doc;
  cur.parseError = null;
  invalidate();
  if (wasBroken) { cur.history = [{ text: cur.savedText, sel: null }]; cur.index = 0; }
  sel = resolveLoc(locate(sel)) || root();
  commit();
  sourceDirty = false;
  fillSource();
  renderAll();
  return true;
}

// ---------- chrome

function refreshChrome() {
  const t = $('#docTitle');
  t.textContent = '';
  if (cur) t.append(cur.path, isDirty(cur) ? h('span', { class: 'dirty', title: 'modified' }, ' ●') : '');
  document.title = (cur ? (isDirty(cur) ? '● ' : '') + cur.path.replace(/^.*\//, '') + ' — ' : '') + 'State Tree Editor';
  $('#btnSave').disabled = !cur;
  $('#btnRevert').disabled = !cur;
  $('#btnUndo').disabled = !cur || !cur.doc || cur.index <= 0 && cur.text === (cur.history[0] || {}).text;
  $('#btnRedo').disabled = !cur || !cur.doc || cur.index >= cur.history.length - 1;
  $('#btnSave').classList.toggle('primary', isDirty(cur));
  const active = document.querySelector('#fileList .file.active');
  if (active) {
    const dot = active.querySelector('.dot');
    if (isDirty(cur) && !dot) active.insertBefore(h('span', { class: 'dot', title: 'modified' }, '●'), active.children[1] || null);
    if (!isDirty(cur) && dot) dot.remove();
  }
}
function renderAll() {
  invalidate();
  if (cur && cur.doc) problems = validateDoc(); else problems = [];
  if (cur) cur.problemCount = problems.filter((p) => p.sev === 'error').length;
  renderOutline();
  renderDetails();
  renderProblems();
  renderFileList();
  refreshChrome();
}

// ---------- keyboard

function inEditable(e) {
  const t = e.target;
  return t && (t.tagName === 'INPUT' || t.tagName === 'TEXTAREA' || t.tagName === 'SELECT' || t.isContentEditable);
}
function moveSelection(dir) {
  if (!rowOrder.length) return;
  const current = rowOf(sel);
  let i = rowOrder.indexOf(current);
  i = i < 0 ? 0 : Math.max(0, Math.min(rowOrder.length - 1, i + dir));
  select(rowOrder[i]);
}
function onKey(e) {
  const ctrl = e.ctrlKey || e.metaKey;
  const key = e.key || '';
  if (key === 'Escape') {
    if (!$('#menu').classList.contains('hidden')) { hideMenu(); return; }
    if (!$('#popup').classList.contains('hidden')) { closePopup(); return; }
  }
  if (ctrl && key.toLowerCase() === 's') { e.preventDefault(); saveFile(); return; }
  if (ctrl && key.toLowerCase() === 'p') { e.preventDefault(); $('#fileFilter').focus(); $('#fileFilter').select(); return; }
  if (ctrl && key.toLowerCase() === 'f' && !sourceOpen) { e.preventDefault(); $('#outlineFind').focus(); $('#outlineFind').select(); return; }
  if (ctrl && e.shiftKey && key.toLowerCase() === 'x') { e.preventDefault(); if (sourceOpen) closeSource(); else openSource(); return; }
  if (key === 'F1') { e.preventDefault(); showHelp(); return; }
  if (sourceOpen && e.target === $('#sourceText')) {
    if (ctrl && key === 'Enter') { e.preventDefault(); applySource(); }
    return;
  }
  if (inEditable(e)) return;
  if (!$('#popup').classList.contains('hidden')) return;
  if (ctrl && !e.shiftKey && key.toLowerCase() === 'z') { e.preventDefault(); undo(); return; }
  if (ctrl && (key.toLowerCase() === 'y' || (e.shiftKey && key.toLowerCase() === 'z'))) { e.preventDefault(); redo(); return; }
  if (!cur || !cur.doc) return;
  const s = sel;
  if (e.altKey && (key === 'ArrowUp' || key === 'ArrowDown')) { e.preventDefault(); opMove(s, key === 'ArrowUp' ? -1 : 1); return; }
  if (ctrl && key.toLowerCase() === 'd') { e.preventDefault(); opDuplicate(s); return; }
  if (ctrl && key.toLowerCase() === 'e') { e.preventDefault(); if (s && ['State', 'Task', 'Transition'].includes(s.tagName)) opToggleEnabled(s); return; }
  if (ctrl || e.altKey) return;
  switch (key) {
    case 'ArrowUp': e.preventDefault(); moveSelection(-1); break;
    case 'ArrowDown': e.preventDefault(); moveSelection(1); break;
    case 'ArrowLeft': {
      e.preventDefault();
      const r = rowOf(s);
      if (r && r.tagName === 'State' && childStates(r).length && !cur.collapsed.has(attr(r, 'name')) && r === s) toggleCollapse(r);
      else if (s !== r && r) select(r);
      else if (r && r.parentNode && r.parentNode.tagName === 'State') select(r.parentNode);
      break;
    }
    case 'ArrowRight': {
      e.preventDefault();
      if (s && s.tagName === 'State' && childStates(s).length) {
        if (cur.collapsed.has(attr(s, 'name'))) toggleCollapse(s); else select(childStates(s)[0]);
      }
      break;
    }
    case 'Delete': e.preventDefault(); opDelete(s); break;
    case 'Insert':
      e.preventDefault();
      if (s && s.tagName === 'State') { if (e.shiftKey) opAddState(s.parentNode, s); else opAddState(s); }
      else opAddState(root());
      break;
    case 't': case 'T': if (s && (s.tagName === 'State' || s === root())) { e.preventDefault(); pickTask(s); } break;
    case 'i': case 'I': if (s && s.tagName === 'Condition') { e.preventDefault(); opInvert(s); } break;
    case 'F2': if (s && s.tagName === 'State') { e.preventDefault(); select(s, { focusName: true }); } break;
    case 'Escape': {
      const up = ownerOf(s, ['State', 'Task', 'Transition', 'Any', 'All', 'Consideration']);
      if (up) select(up); else if (s !== root()) select(root());
      break;
    }
    case 'Enter': if (s) { e.preventDefault(); const i = $('#details input, #details select'); if (i) i.focus(); } break;
  }
}

// ---------- splitters, theme

function initSplitters() {
  const rootStyle = document.documentElement.style;
  for (const name of ['left', 'right', 'bottom']) {
    const saved = storageGet('ste.' + name, '');
    if (saved) rootStyle.setProperty('--' + name, saved);
  }
  for (const sp of document.querySelectorAll('.splitter')) {
    sp.addEventListener('pointerdown', (e) => {
      e.preventDefault();
      sp.setPointerCapture(e.pointerId);
      sp.classList.add('active');
      const which = sp.dataset.split;
      const move = (ev) => {
        let v;
        if (which === 'left') v = Math.max(140, Math.min(window.innerWidth * 0.4, ev.clientX));
        else if (which === 'right') v = Math.max(260, Math.min(window.innerWidth * 0.6, window.innerWidth - ev.clientX));
        else v = Math.max(60, Math.min(window.innerHeight * 0.6, window.innerHeight - ev.clientY));
        rootStyle.setProperty('--' + which, Math.round(v) + 'px');
      };
      const up = () => {
        sp.classList.remove('active');
        sp.removeEventListener('pointermove', move);
        sp.removeEventListener('pointerup', up);
        storageSet('ste.' + which, rootStyle.getPropertyValue('--' + which));
      };
      sp.addEventListener('pointermove', move);
      sp.addEventListener('pointerup', up);
    });
  }
}
function applyTheme(theme) {
  document.documentElement.dataset.theme = theme;
  storageSet('ste.theme', theme);
}

// ---------- selftest (?selftest=1): every file through parse + serialize, plus a few edit round trips

async function runSelftest() {
  const box = $('#selftest');
  box.classList.remove('hidden');
  box.textContent = '';
  const status = h('div', null, 'running...');
  box.append(h('h2', null, 'Selftest: round trip of every tree file'), status);
  const results = [];
  const savedCur = cur;
  const savedD = D;
  for (const f of files) {
    const r = { path: f.path, ok: false, detail: '', errors: 0, warnings: 0, infos: 0, edits: '' };
    try {
      const { body } = await api('/api/file?path=' + encodeURIComponent(f.path));
      const text = body.replace(/\r\n/g, '\n');
      const p = parseXml(text);
      if (p.error) { r.detail = 'parse error: ' + p.error; results.push(r); continue; }
      const out = serialize(p.doc);
      r.ok = out === text;
      if (!r.ok) {
        const a = text.split('\n');
        const b = out.split('\n');
        let i = 0;
        while (i < a.length && a[i] === b[i]) i++;
        r.detail = 'line ' + (i + 1) + '\nfile:  ' + JSON.stringify(a[i]) + '\nwrote: ' + JSON.stringify(b[i]);
      }
      cur = { path: f.path, collapsed: new Set() };
      D = p.doc;
      invalidate();
      const found = validateDoc();
      for (const x of found) r[x.sev === 'error' ? 'errors' : x.sev === 'warning' ? 'warnings' : 'infos']++;
      r.messages = found.filter((x) => x.sev !== 'info').map((x) => x.sev + ': ' + x.msg + '  (' + whereLabel(x.el) + ')');
      const first = kids(D.documentElement, 'State')[0];
      if (first) {
        const copy = first.cloneNode(true);
        first.parentNode.insertBefore(copy, first.nextSibling);
        copy.remove();
        if (moveSibling(first, 1)) { invalidate(); moveSibling(first, -1); }
        invalidate();
        const before = serialize(D);
        const had = getComment(first);
        setComment(first, had + '\nx');
        setComment(first, had);
        if (serialize(D) !== before || before !== out) r.edits = 'edit round trip changed the text';
      }
    } catch (e) {
      r.detail = 'exception: ' + e.message;
    }
    results.push(r);
  }
  cur = savedCur;
  D = savedD;
  invalidate();
  const failed = results.filter((r) => !r.ok || r.edits);
  const pass = !failed.length && results.length > 0;
  const totals = results.reduce((t, r) => { t.e += r.errors; t.w += r.warnings; t.i += r.infos; return t; }, { e: 0, w: 0, i: 0 });
  status.textContent = '';
  status.append(h('div', { class: pass ? 'pass' : 'fail', style: 'font-size:15px;font-weight:600' },
    (pass ? 'PASS' : 'FAIL') + ': ' + (results.length - failed.length) + ' / ' + results.length + ' files reproduced byte for byte (line endings normalized)'),
  h('div', { class: 'hint' }, 'validation over all files: ' + totals.e + ' errors, ' + totals.w + ' warnings, ' + totals.i + ' infos'),
  h('div', { class: 'toolbar-row' }, h('button', { onclick: () => box.classList.add('hidden') }, 'Close')));
  const table = h('table', null, h('tr', null, h('th', null, 'file'), h('th', null, 'round trip'), h('th', null, 'errors'), h('th', null, 'warnings'), h('th', null, 'details')));
  for (const r of results) {
    table.append(h('tr', null, h('td', { class: 'mono' }, r.path), h('td', { class: r.ok && !r.edits ? 'pass' : 'fail' }, r.ok && !r.edits ? 'identical' : 'MISMATCH'),
      h('td', { class: r.errors ? 'fail' : '' }, String(r.errors)), h('td', null, String(r.warnings)),
      h('td', null, h('pre', null, [r.detail, r.edits].concat(r.messages || []).filter(Boolean).join('\n')))));
  }
  box.append(table);
  window.selftestResult = { pass, total: results.length, failed: failed.map((r) => r.path), totals, results: results.map((r) => ({ path: r.path, ok: r.ok, edits: r.edits, errors: r.errors, warnings: r.warnings, detail: r.detail, messages: r.messages })) };
  document.title = (pass ? 'SELFTEST PASS' : 'SELFTEST FAIL') + ' — State Tree Editor';
  return window.selftestResult;
}

// ---------- init

async function init() {
  applyTheme(storageGet('ste.theme', 'dark'));
  initSplitters();
  $('#btnNew').addEventListener('click', newFile);
  $('#btnSave').addEventListener('click', saveFile);
  $('#btnRevert').addEventListener('click', revertFile);
  $('#btnUndo').addEventListener('click', undo);
  $('#btnRedo').addEventListener('click', redo);
  $('#btnSource').addEventListener('click', () => { if (sourceOpen) closeSource(); else openSource(); });
  $('#btnSourceApply').addEventListener('click', applySource);
  $('#btnSourceClose').addEventListener('click', closeSource);
  $('#sourceText').addEventListener('input', () => { sourceDirty = true; });
  $('#btnTheme').addEventListener('click', () => applyTheme(document.documentElement.dataset.theme === 'light' ? 'dark' : 'light'));
  $('#btnHelp').addEventListener('click', showHelp);
  $('#btnExpandAll').addEventListener('click', () => setAllCollapsed(false));
  $('#btnCollapseAll').addEventListener('click', () => setAllCollapsed(true));
  $('#fileFilter').addEventListener('input', renderFileList);
  $('#fileFilter').addEventListener('keydown', (e) => {
    if (e.key === 'Enter') { const first = document.querySelector('#fileList .file'); if (first) first.click(); }
    if (e.key === 'Escape') { e.target.value = ''; renderFileList(); $('#outline').focus(); }
  });
  $('#outlineFind').addEventListener('input', (e) => { findText = e.target.value.trim(); renderOutline(); });
  $('#outlineFind').addEventListener('keydown', (e) => {
    if (e.key === 'Enter') {
      const matches = rowOrder.filter((el) => el.tagName === 'State' && matchesFind(el));
      if (!matches.length) return;
      const i = matches.indexOf(rowOf(sel));
      select(matches[(i + (e.shiftKey ? -1 : 1) + matches.length) % matches.length]);
    }
    if (e.key === 'Escape') { e.target.value = ''; findText = ''; renderOutline(); $('#outline').focus(); }
  });
  $('#showWarnings').addEventListener('change', renderProblems);
  $('#showInfos').addEventListener('change', renderProblems);
  document.addEventListener('keydown', onKey);
  document.addEventListener('mousedown', (e) => {
    if (!$('#menu').contains(e.target)) hideMenu();
    if (!$('#popup').classList.contains('hidden') && !$('#popup').contains(e.target)) closePopup();
  });
  window.addEventListener('beforeunload', (e) => { if (anyDirty()) { e.preventDefault(); e.returnValue = ''; } });
  window.addEventListener('hashchange', () => { const p = decodeURI(location.hash.slice(1)); if (p && (!cur || p !== cur.path)) openFile(p); });
  try {
    const { body } = await api('/schema.json');
    schema = body;
  } catch (e) {
    toast('schema.json not loaded (run gen_schema.py): ' + e.message, 'error');
  }
  try {
    await loadFiles();
  } catch (e) {
    toast('cannot list the files: ' + e.message, 'error');
  }
  renderAll();
  if (new URLSearchParams(location.search).get('selftest')) { await runSelftest(); return; }
  const wanted = decodeURI(location.hash.slice(1)) || storageGet('ste.last', '');
  const target = files.find((f) => f.path === wanted) || files[0];
  if (target) await openFile(target.path);
}

window.STE = { runSelftest, serialize, parseXml, validate: () => validateDoc(), get doc() { return D; } };
init();
})();
