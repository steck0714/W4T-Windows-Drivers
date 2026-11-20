r"""Line-by-line model of drivers/t214-aula/kit/agent/findagent.cmd.
Models: letter table, mountvol assign/delete, cmd 'if exist' on letters and on \\?\Volume{GUID}\ paths.
It does NOT model cmd parsing; it checks the algorithm under explicit assumptions (see parameters)."""
import itertools, string, sys

ALL = list(string.ascii_uppercase)
ASSIGN_ORDER = list("ZYXWVUTSRQPONMLKJIHGFED")

class Vol:
    def __init__(s, guid, letter=None, marker=False, assignable=True):
        s.guid, s.letter, s.marker, s.assignable = guid, letter, marker, assignable

def run(vols, table, unassignable, guid_open, alias_ok, trace=None):
    """table: letter->Vol (mutated). unassignable: letters that exist as devices (empty reader) but 'if exist X:\\' is false
    and mountvol cannot use them. Returns AGENT ('' = none)."""
    def exists_letter(L): return L in table
    def marker_at_letter(L): return L in table and table[L].marker
    def assign(v):
        for L in ASSIGN_ORDER:
            if not exists_letter(L):
                ok = v.assignable and L not in unassignable and (alias_ok or not any(x is v for x in table.values()))
                if ok:
                    table[L] = v; return L
        return ""
    FOUND = ""
    # step 1
    for L in ALL:
        if not FOUND and marker_at_letter(L): FOUND = L
    if FOUND: return FOUND
    # step 2 (:viavolume)
    for v in vols:
        if FOUND: break
        if guid_open and v.marker:
            A = assign(v)
            if A and marker_at_letter(A): FOUND = A
    if FOUND: return FOUND
    # step 3 (:trymount)
    for v in vols:
        if FOUND: break
        A = assign(v)
        if not A: continue
        if marker_at_letter(A): FOUND = A
        else: del table[A]
    return FOUND

def build(scn):
    vols=[]; table={}
    c = Vol("C-windows", "C"); vols.append(c); table["C"]=c
    p = Vol("P-boot", scn["p_letter"], marker=True, assignable=scn["assign_ok"])
    vols.append(p)
    if scn["p_letter"]: table[scn["p_letter"]] = p
    for i in range(scn["decoys"]):
        d = Vol(f"decoy{i}", None, marker=(scn["dup_marker"] and i==0), assignable=scn["assign_ok"]); vols.append(d)
    if scn["other_letter_vol"]:
        o = Vol("usb", "F" if scn["p_letter"]!="F" else "G"); vols.append(o); table[o.letter]=o
    if scn["order"]=="p_last": vols.append(vols.pop(1))
    return vols, table

def free_pressure(level):
    # which letters are taken by empty card readers etc. (device present, no media, cannot be reused)
    if level=="none": return set()
    if level=="most": return set("ZYXWVUTSRQPONMLKJIHGFE")          # only D free
    if level=="all":  return set(ASSIGN_ORDER)                     # nothing assignable
    return set("ZYX")

bad=[]; n=0; stats={}
for p_letter in [None,"D","E","Z","K"]:
  for assign_ok in [True,False]:
    for guid_open in [True,False]:
      for alias_ok in [True,False]:
        for decoys in [0,1,3]:
          for dup_marker in [False,True]:
            if dup_marker and decoys==0: continue
            for other in [False,True]:
              for order in ["p_first","p_last"]:
                for press in ["none","some","most","all"]:
                  scn=dict(p_letter=p_letter,assign_ok=assign_ok,guid_open=guid_open,alias_ok=alias_ok,decoys=decoys,
                           dup_marker=dup_marker,other_letter_vol=other,order=order,press=press)
                  vols,table=build(scn); un=free_pressure(press)
                  # letters in 'un' that are already used by real volumes are not 'unassignable devices'
                  un={L for L in un if L not in table}
                  before=dict(table)
                  got=run(vols,table,un,guid_open,alias_ok); n+=1
                  p=vols[1] if order=="p_first" else vols[-1]
                  p=[v for v in vols if v.guid=="P-boot"][0]
                  # feasibility under the assumptions
                  free_letters=[L for L in ASSIGN_ORDER if L not in before and L not in un]
                  feasible = (p_letter is not None) or (assign_ok and free_letters and (guid_open or True))
                  # (step 3 works without guid_open as long as a letter can be assigned)
                  # invariants
                  errs=[]
                  if feasible and not got: errs.append("FEASIBLE but not found")
                  if not feasible and got: errs.append("found although infeasible?")
                  if got and not table.get(got,Vol("x")).marker: errs.append("AGENT letter does not hold a marker volume")
                  for L,v in before.items():
                      if table.get(L) is not v: errs.append(f"pre-existing mapping {L}: changed")
                  for L,v in table.items():
                      if L in ("A","B","C") and before.get(L) is not v: errs.append(f"touched {L}:")
                  stray=[L for L,v in table.items() if L not in before and not (v.marker and L==got)]
                  if stray: errs.append(f"stray temporary letters left: {stray}")
                  extra_marker=[L for L,v in table.items() if L not in before and v.marker and L!=got]
                  if extra_marker: errs.append(f"extra letters on marker volumes: {extra_marker}")
                  if not got: 
                      pass
                  # idempotency: second run must hit step 1
                  if got:
                      got2=run(vols,table,un,guid_open,alias_ok)
                      if not got2: errs.append("2nd run lost it")
                  key=("found" if got else "NOT found")
                  stats[(p_letter,key)]=stats.get((p_letter,key),0)+1
                  if errs: bad.append((scn,got,errs))
print("scenarios simulated:",n)
print("invariant violations:",len(bad))
for scn,got,errs in bad[:12]:
    print(" -",{k:v for k,v in scn.items()},"->AGENT=",repr(got),errs)
print("\noutcome by SD partition letter:")
for k in sorted(stats,key=str): print("  ",k,stats[k])
