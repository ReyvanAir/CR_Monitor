"""CR Monitor enclosure, revision A — Blender 4.5 LTS.

Run in Blender's Scripting workspace (Run Script), or:
    blender --background --python build_enclosure.py
All mesh coordinates and exported STL coordinates are millimetres.
The electronics are reference envelopes, not manufacturer CAD.
The script creates a NEW scene. Existing scenes are preserved.
Edit P below before regeneration. See README.md for fit checks.
"""
import bpy, bmesh, math, json, os, struct
from pathlib import Path
from mathutils import Vector

OUT = Path(os.environ.get('CR_MONITOR_OUTPUT', str(Path(__file__).resolve().parent) if '__file__' in globals() else bpy.path.abspath('//CR_Monitor_Enclosure')))
OUT.mkdir(parents=True, exist_ok=True)
(OUT/'STL').mkdir(exist_ok=True)
(OUT/'previews').mkdir(exist_ok=True)
P = {
    'body_w':186.0, 'body_h':136.0, 'body_d':62.0,
    'wall':2.4, 'back_floor':3.0, 'front_thickness':3.0,
    'corner_radius':9.0, 'seam_gap':0.30, 'skirt_clearance':0.35,
    'partition_x':39.0, 'sensor_x':65.0,
    'tft_pcb_w':98.0, 'tft_pcb_h':64.0, 'tft_pcb_thickness':1.6,
    'tft_center_x':-28.0, 'tft_center_y':17.0,
    'tft_window_w':76.0, 'tft_window_h':51.0,
    'tft_glass_w':84.0, 'tft_glass_h':57.0, 'tft_glass_depth':4.6,
    'tft_active_w':73.9, 'tft_active_h':49.3,
    'tft_window_offset_x':0.0, 'tft_window_offset_y':0.0,
    'tft_wired_depth_below_pcb':19.0,
    'esp_pcb_length':64.0, 'esp_pcb_width':27.0,
    'esp_center_x':-55.0, 'esp_center_y':30.0,
    'battery_holder_w':84.0, 'battery_holder_h':44.0, 'battery_holder_d':23.0,
    'battery_center_x':-33.5, 'battery_center_y':-37.0,
    'cell_diameter':18.6, 'cell_length':65.5,
    'power_envelope_w':42.0, 'power_envelope_h':32.0, 'power_envelope_d':20.0,
    'power_center_x':14.5, 'power_center_y':28.0,
    'sensor_boards':{'BH1750':[31.0,16.0], 'MAX4466':[20.0,14.0], 'SHT3x':[23.0,18.0]},
    'sensor_y':{'BH1750':40.0, 'MAX4466':0.0, 'SHT3x':-39.0},
    'sensor_board_bottom':{'BH1750':56.0, 'MAX4466':50.5, 'SHT3x':54.0},
    'sensor_xy_offsets':{'BH1750':[0.0,0.0], 'MAX4466':[0.0,0.0], 'SHT3x':[0.0,0.0]},
    'case_insert_bore':4.2, 'case_insert_depth':5.7,
    'pilot_diameter':2.8, 'clearance_hole':3.4,
    'render':True, 'render_samples':40,
}

scene = bpy.data.scenes.new('CR Monitor | assembled')
bpy.context.window.scene = scene
# drop Blender's untouched startup scene so the file opens on this one, not the default cube
for _s in list(bpy.data.scenes):
    if _s is not scene and {o.name for o in _s.objects} == {'Cube','Camera','Light'}:
        bpy.data.scenes.remove(_s)
scene.unit_settings.system='METRIC'
scene.unit_settings.scale_length=0.001
scene.unit_settings.length_unit='MILLIMETERS'
scene['Design status']='Revision A: dimensioned prototype; measure actual boards and holder before printing.'
scene['Documentation']='https://reyvanair.github.io/CR_Monitor/hardware.html'
scene['Coordinates']='X = width; Y = up when wall mounted; Z = rear to front. Coordinates in mm.'

def collection(name):
    c=bpy.data.collections.new(name); scene.collection.children.link(c); return c
rear_c=collection('01 PRINT | rear tray')
lid_c=collection('02 PRINT | front bezel')
mount_c=collection('03 PRINT | removable mounts')
hw_c=collection('04 REFERENCE ONLY | electronics, fasteners, foam')
label_c=collection('05 PRESENTATION ONLY | legends')
keep_c=collection('06 FIT ENVELOPES | hidden in render')
studio_c=collection('07 STUDIO | cameras and lights')
temp_c=collection('_construction')
keep_c.hide_render=True
parts=[]; components=[]; front_group=[]; base_group=[]

def material(name,col,metal=0,rough=.45,emit=0):
    m=bpy.data.materials.new(name);m.diffuse_color=(*col,1);m.use_nodes=True
    b=m.node_tree.nodes.get('Principled BSDF');b.inputs['Base Color'].default_value=(*col,1)
    b.inputs['Roughness'].default_value=rough;b.inputs['Metallic'].default_value=metal
    if emit:
        b.inputs['Emission Color'].default_value=(*col,1);b.inputs['Emission Strength'].default_value=emit
    return m
ivory=material('Warm white | printed front',(0.78,.81,.79),rough=.3)
navy=material('Deep petrol | printed rear',(.023,.075,.091),rough=.35)
teal=material('Teal | removable carriers',(.025,.40,.38),rough=.36)
dark=material('Black polymer',(.012,.019,.024),rough=.42)
pcb=material('PCB green',(.016,.18,.104),rough=.45)
redpcb=material('Shield PCB red',(.29,.025,.027),rough=.4)
metal=material('Nickel and steel',(.42,.48,.51),metal=.85,rough=.24)
gold=material('Contacts',(.63,.4,.075),metal=.75,rough=.22)
purple=material('18650 sleeves',(.22,.095,.37),rough=.32)
ink=material('Legend ink',(.032,.085,.10),rough=.6)
white=material('UI white',(.83,.9,.91),emit=.45)
muted=material('UI muted',(.26,.4,.46),emit=.3)
ui_bg=material('LCD dark',(.008,.019,.028),rough=.25,emit=.25)
ui_card=material('LCD cards',(.018,.044,.062),rough=.4,emit=.35)
green=material('LCD green',(.12,.7,.43),emit=.65)
blue=material('LCD blue',(.16,.53,.87),emit=.55)
amber=material('LCD amber',(.85,.49,.13),emit=.6)

def move_col(o,c):
    for oc in list(o.users_collection):oc.objects.unlink(o)
    c.objects.link(o)
def assign(o,m,c):
    move_col(o,c)
    if m:o.data.materials.append(m)
    return o
def rounded(name,w,h,r,z0,z1,x=0,y=0,c=temp_c,m=None,n=8):
    r=min(r,w/2-.001,h/2-.001)
    if r<=0:pts=[(-w/2,-h/2),(w/2,-h/2),(w/2,h/2),(-w/2,h/2)]
    else:
        pts=[]
        for cx,cy,a in [(w/2-r,h/2-r,0),(-w/2+r,h/2-r,90),(-w/2+r,-h/2+r,180),(w/2-r,-h/2+r,270)]:
            for i in range(n+1):
                t=math.radians(a+90*i/n);pts.append((cx+r*math.cos(t),cy+r*math.sin(t)))
    N=len(pts);vs=[(u+x,v+y,z) for z in [z0,z1] for u,v in pts]
    fs=[tuple(reversed(range(N))),tuple(range(N,2*N))]
    fs.extend((i,(i+1)%N,(i+1)%N+N,i+N) for i in range(N))
    mesh=bpy.data.meshes.new(name);mesh.from_pydata(vs,[],fs);mesh.update()
    o=bpy.data.objects.new(name,mesh);c.objects.link(o)
    if m:mesh.materials.append(m)
    return o
def box(name,size,center,c=temp_c,m=None,r=0):
    w,h,d=size;x,y,z=center;return rounded(name,w,h,r,z-d/2,z+d/2,x,y,c,m)
def cyl(name,r,d,center,c=temp_c,m=None,axis='Z',r2=None,verts=48):
    rot={'Z':(0,0,0),'X':(0,math.pi/2,0),'Y':(math.pi/2,0,0)}[axis]
    if r2 is None:bpy.ops.mesh.primitive_cylinder_add(vertices=verts,radius=r,depth=d,location=center,rotation=rot)
    else:bpy.ops.mesh.primitive_cone_add(vertices=verts,radius1=r,radius2=r2,depth=d,location=center,rotation=rot)
    o=bpy.context.object;o.name=name;assign(o,m,c);return o
def active(o):
    bpy.ops.object.select_all(action='DESELECT');o.hide_set(False);o.select_set(True);bpy.context.view_layer.objects.active=o
def boolean(o,cut,op='DIFFERENCE'):
    active(o);md=o.modifiers.new(op,'BOOLEAN');md.operation=op;md.solver='EXACT';md.object=cut
    bpy.ops.object.modifier_apply(modifier=md.name);bpy.data.objects.remove(cut,do_unlink=True)
    return o
def union(o,*others):
    for q in others:boolean(o,q,'UNION')
    return o
def bevel(o,width=.4,segments=3):
    active(o);md=o.modifiers.new('Edge easing','BEVEL');md.width=width;md.segments=segments
    bpy.ops.object.modifier_apply(modifier=md.name)
    return o
def hole(o,x,y,z0,z1,d):return boolean(o,cyl('bore',d/2,z1-z0,(x,y,(z0+z1)/2)))
def register(o,filename,group='base',note=''):
    o['Printable']=True;o['STL filename']=filename;o['Fit note']=note
    parts.append((o,filename));(front_group if group=='front' else base_group).append(o);return o
def ref(o,note='',front=False):
    o['Printable']=False;o['Reference only']=note
    components.append(o)
    if front:front_group.append(o)
    return o
def text3(name,body,pos,size,mat=ink,c=label_c,align='LEFT',front=False):
    cv=bpy.data.curves.new(name,'FONT');cv.body=body;cv.size=size;cv.align_x=align;cv.extrude=0
    o=bpy.data.objects.new(name,cv);c.objects.link(o);o.location=pos;cv.materials.append(mat)
    if front:front_group.append(o)
    return o
def envelope(name,size,center,note):
    o=box(name,size,center,c=keep_c);o.display_type='WIRE';o.hide_render=True;o['Fit note']=note
    o.hide_set(True);return o

W,H,D=P['body_w'],P['body_h'],P['body_d'];t=P['wall'];floor=P['back_floor']
F=D-P['front_thickness'];T=F-P['seam_gap'];sx=P['sensor_x'];px=P['partition_x']
tx,ty=P['tft_center_x'],P['tft_center_y'];bx,by=P['battery_center_x'],P['battery_center_y']
case_screws=[(a*(W/2-9),b*(H/2-9)) for a in [-1,1] for b in [-1,1]]

# Rear shell: one fused solid, with four blind insert bosses.
rear=rounded('P01 | rear tray',W,H,P['corner_radius'],0,T,c=rear_c,m=navy)
bevel(rear,.55)
boolean(rear,rounded('cavity',W-2*t,H-2*t,P['corner_radius']-t,floor,T+2))
for x,y in case_screws:
    union(rear,cyl('case boss',4.75,T-2.5,(x,y,(T+2.5)/2)))
    union(rear,box('boss reinforcing web',(8,2.4,F-6.5),(x+(4 if x>0 else -4),y,(F-4+2.5)/2)))
    hole(rear,x,y,T-14,T+.5,3.3)
    hole(rear,x,y,T-P['case_insert_depth'],T+.5,P['case_insert_bore'])

# Full-height thermal partition and two separate sensor cells.
union(rear,box('thermal partition',(2.4,H-2*t,55.4),(px,0,30.7)))
for y in [-18,19]:
    union(rear,box('sensor separator',(W/2-t-px+1.2,2.2,55.4),((px-1.2+W/2-t)/2,y,30.7)))
for y in [-39,0,40]:
    boolean(rear,box('wire pass',(7,9,6),(px,y,17),r=0))

# Main electronics exhaust, plus crossflow vents for the SHT3x cell.
for x in [-66,-47,-28,-9,10,29]:
    boolean(rear,box('top exhaust',(11,8,3),(x,H/2,43)))
for z in [10,17,24,31,38,45]:
    boolean(rear,box('sensor side vent',(8,29,2.8),(W/2,-39,z)))
for z in [12,21,30,39,48]:
    boolean(rear,box('sensor bottom vent',(27,8,2.8),(sx,-H/2,z)))

# USB pair opening; second opening is for a panel cable from chosen power module.
boolean(rear,box('USB service opening',(9,31,16),(-W/2,30,14)))
boolean(rear,box('power cable opening',(9,14,10),(-W/2,-2,14)))
for yy in [25,35]:hole(rear,-78,yy,-1,4,3.2)

# Wall keyholes in the rear. Screws supplied separately; head pockets kept clear.
for x in [-65,20]:
    hole(rear,x,55,-1,4,8)
    hole(rear,x,61,-1,4,4.4)
    boolean(rear,box('keyhole neck',(4.4,6,6),(x,58,1.5)))

def base_boss(x,y,top,rad=3.7):
    union(rear,cyl('mount boss',rad,top-2.7,(x,y,(top+2.7)/2)))
    hole(rear,x,y,1.8,top+.3,P['pilot_diameter'])

# Battery cradle mounts are outside the purchased holder footprint.
for x in [bx-46.5,bx+46.5]:base_boss(x,by,5.0)
holder_w,holder_h=P['battery_holder_w'],P['battery_holder_h']
cradle=rounded('P03 | battery holder cradle',holder_w+5.8,holder_h+5.8,3,5,13.0,bx,by,c=mount_c,m=teal)
boolean(cradle,rounded('holder recess',holder_w+1,holder_h+1,1.0,7.4,15,bx,by))
for x in [bx-46.5,bx+46.5]:
    union(cradle,cyl('mount ear',4.1,2.4,(x,by,6.2)))
    hole(cradle,x,by,4.5,8.0,3.4)
for x in [bx-25,bx+25]:
    for y in [by-holder_h/2-1,by+holder_h/2+1]:
        boolean(cradle,box('strap passage',(7,8,3),(x,y,9.8)))
register(cradle,'03_battery_cradle.stl',note='Fits a purchased insulated 84 x 44 x 23 mm two-cell holder; straps go through side slots.')

# Two modular rear carriers. Tie slots retain the boards without relying on unknown PCB holes.
def rear_carrier(name,filename,cx,cy,w,h,board_bottom,ear_axis='X'):
    plate=rounded(name,w,h,2,board_bottom-2.4,board_bottom,cx,cy,c=mount_c,m=teal)
    if ear_axis=='X': pts=[(cx-w/2-2.5,cy),(cx+w/2+2.5,cy)]
    else:pts=[(cx,cy-h/2-2.5),(cx,cy+h/2+2.5)]
    for x,y in pts:
        union(plate,cyl('mount ear',3.8,2.4,(x,y,board_bottom-1.2)))
        base_boss(x,y,board_bottom-2.4)
        hole(plate,x,y,board_bottom-3,board_bottom+.3,3.4)
    for x in [cx-w*.31,cx+w*.31]:
        for y in [cy-h/2+3,cy+h/2-3]:
            boolean(plate,box('nylon tie slot',(3.5,2.0,4),(x,y,board_bottom-1.2)))
    return register(plate,filename,note='Use nonconductive ties and edge shims; board reference dimensions are assumptions.')
esp=rear_carrier('P04 | ESP32 carrier','04_esp32_carrier.stl',-55,30,67,33,9.5,'Y')
boolean(esp,rounded('ESP underside components clearance',58,22,1,6.5,10,-55,30))
boolean(esp,box('ESP underside USB clearance',(10,22,5),(-86,30,8)))
esp['Fit note']='Component side faces rear; long header pins face TFT. Open frame clears shield and USB bodies. Verify board and rear probe positions.'
power=rear_carrier('P05 | power module carrier','05_power_carrier.stl',P['power_center_x'],28,44,35,7.9,'Y')

# Baffled cable anchors in clear areas of the tray.
for x,y in [(-79,-1),(-9,-2),(27,-7)]:
    anchor=box('wire anchor',(9,5,6),(x,y,5.5),r=1)
    boolean(anchor,box('tie bore',(5,9,2.5),(x,y,5.5)))
    union(rear,anchor)
register(rear,'01_rear_tray.stl')

# Front shell with registration lip. Corner reliefs avoid the rear screw bosses.
lid=rounded('P02 | front bezel',W,H,P['corner_radius'],F,D,c=lid_c,m=ivory)
bevel(lid,.45)
skirt=rounded('registration skirt',W-2*t-2*P['skirt_clearance'],H-2*t-2*P['skirt_clearance'],P['corner_radius']-t-.35,F-3.3,F+.15)
boolean(skirt,rounded('skirt hollow',W-2*t-2*P['skirt_clearance']-3.2,H-2*t-2*P['skirt_clearance']-3.2,4.5,F-4,F+1))
for x,y in case_screws:hole(skirt,x,y,F-4,F+1,12.0)
# Skirt is relieved where it would cross sensor partition ends.
for y in [-18,19]:boolean(skirt,box('skirt separator relief',(9,3.3,5),(W/2-t-1,y,F-1)))
for y in [-H/2+t+1,H/2-t-1]:boolean(skirt,box('skirt partition relief',(3.4,9,5),(px,y,F-1)))
union(lid,skirt)
wx=tx+P['tft_window_offset_x'];wy=ty+P['tft_window_offset_y']
boolean(lid,rounded('display window',P['tft_window_w'],P['tft_window_h'],.7,F-5,D+1,wx,wy))
for x,y in case_screws:
    hole(lid,x,y,F-4,D+1,P['clearance_hole'])
    boolean(lid,cyl('countersink',1.7,1.6,(x,y,D-.7),r2=3.3))

# Sensor apertures: clear optical well, short microphone port, open ventilation slots.
boolean(lid,rounded('BH1750 optical aperture',22,16,3,F-3,D+1,sx,40))
hole(lid,sx,0,F-3,D+1,8)
for y in [-51,-47,-43,-39,-35,-31,-27]:
    boolean(lid,rounded('SHT3x front vent',29,2,1,F-2,D+1,sx,y,n=5))

# Display PCB back retainers. Long slots permit +/- 1.5 mm clamp adjustment.
tft_pcb_bottom=F-.2-P['tft_glass_depth']-P['tft_pcb_thickness']
clip_top=tft_pcb_bottom-.2
for side in [-1,1]:
    postx=tx+side*(P['tft_pcb_w']/2+6.5)
    for j,cy in enumerate([ty-17,ty+17]):
        union(lid,cyl('display clamp post',3.75,F-clip_top+.1,(postx,cy,(F+.1+clip_top)/2)))
        hole(lid,postx,cy,clip_top-.5,D-1.4,2.8)
        cx=postx-side*4.5
        clip=rounded('P06 | TFT clamp %s %s'%(side,j),18,10,1.4,clip_top-2.4,clip_top,cx,cy,c=mount_c,m=teal)
        boolean(clip,rounded('adjustment slot',6.5,3.4,1.7,clip_top-3,clip_top+1,postx,cy))
        register(clip,'06_tft_clamp_%s_%s.stl'%('L' if side<0 else 'R',j+1),'front',note='Clamps touch bare PCB edges only. Reposition to avoid actual shield headers.')

# Sensor plates use edge rails + two slim nylon ties. Bosses are all in the removable lid.
for k,(bw,bh) in P['sensor_boards'].items():
    cy=P['sensor_y'][k];z=P['sensor_board_bottom'][k]
    carrier=rounded('P07 | '+k+' carrier',47,28,2,z-2.2,z,sx,cy,c=mount_c,m=teal)
    boolean(carrier,rounded('component underside clearance',bw-4,bh-4,1,z-3,z+1,sx,cy))
    for x in [sx-20.5,sx+20.5]:
        union(lid,cyl(k+' carrier post',3.1,F-z+.15,(x,cy,(F+.15+z)/2)))
        hole(lid,x,cy,z-.5,D-1.4,2.8)
        hole(carrier,x,cy,z-3,z+1,3.4)
    for x in [sx-bw/2+3,sx+bw/2-3]:
        for y in [cy-10.5,cy+10.5]:
            boolean(carrier,box('sensor tie slot',(3.4,1.6,4),(x,y,z-1)))
    for y in [cy-bh/2-1.15,cy+bh/2+1.15]:
        union(carrier,box('PCB edge guide',(7,1.5,1.4),(sx,y,z+.6)))
    register(carrier,'07_'+k.lower()+'_carrier.stl','front',note='Board envelope %g x %g mm. Align sensing element with aperture; shim/adjust XY as required.'%(bw,bh))

# Piezo cup, open from rear, with front perforations. Foam tape retains the actual buzzer.
bzx,bzy=21,-44
ring=cyl('buzzer cup',8.9,10.2,(bzx,bzy,F-5))
hole(ring,bzx,bzy,F-11,F+1,13.4)
union(lid,ring)
for x,y in [(0,0),(-3,0),(3,0),(0,-3),(0,3),(-2.2,-2.2),(2.2,2.2)]:
    hole(lid,bzx+x,bzy+y,F-.5,D+1,1.8)
register(lid,'02_front_bezel.stl','front')

# Reference hardware: deliberately separate from printable geometry.
hold=rounded('REF | purchased insulated 2x18650 holder',holder_w,holder_h,2,7.4,9.9,bx,by,c=hw_c,m=dark)
ref(hold,'Nominal purchased holder. No printed electrical contacts.')
for yy in [by-10,by+10]:
    ref(cyl('REF | 18650 cell',P['cell_diameter']/2,P['cell_length'],(bx,yy,20.2),c=hw_c,m=purple,axis='X'),'Nominal cell size. Actual protected/button-top cells may be longer.')
    for xx in [bx-P['cell_length']/2,bx+P['cell_length']/2]:
        ref(cyl('REF | cell end',8.3,.5,(xx,yy,20.2),c=hw_c,m=metal,axis='X'))
for xx in [bx-holder_w/2+2,bx+holder_w/2-2]:
    ref(box('REF | holder end wall',(4,holder_h,16),(xx,by,19.4),c=hw_c,m=dark,r=.7))
for yy in [by-holder_h/2+1.3,by+holder_h/2-1.3]:
    ref(box('REF | holder side wall',(holder_w,2.6,10),(bx,yy,16),c=hw_c,m=dark))

ref(box('REF | ESP32-S3 DevKitC-1 PCB',(P['esp_pcb_length'],P['esp_pcb_width'],1.6),(-55,30,10.3),c=hw_c,m=pcb,r=1),'64 x 27 mm assumed envelope. Components face rear; long header pins face TFT.')
ref(box('REF | ESP32 module shield',(19,18,2.8),(-43,30,8.1),c=hw_c,m=metal,r=.6))
ref(box('REF | ESP32 antenna',(7,18,.2),(-27,30,9.4),c=hw_c,m=dark))
for yy in [19,41]:
    ref(box('REF | ESP header',(55,2.6,2.5),(-54,yy,12.35),c=hw_c,m=dark))
    for xx in range(-79,-25,3):ref(cyl('REF | ESP pin',.3,6,(xx,yy,16.6),c=hw_c,m=gold,verts=12))
for yy in [23.7,36.3]:ref(box('REF | USB connector',(6,8,3),(-87.8,yy,8),c=hw_c,m=metal,r=.5))
for yy in [25,35]:ref(box('REF | boot-reset switch',(3,3,1.5),(-78,yy,8.75),c=hw_c,m=dark))

ref(box('REF | power module RESERVED ENVELOPE',(42,32,2),(P['power_center_x'],28,9.0),c=hw_c,m=pcb,r=1),'Power electronics not specified by documentation. Reserve only; select compatible protected regulated module.')
ref(box('REF | inductor proxy',(12,12,7),(P['power_center_x']-1,28,13.4),c=hw_c,m=dark,r=1))
ref(cyl('REF | power capacitor',3,6,(P['power_center_x']+13,31,13),c=hw_c,m=metal))

ref(box('REF | ILI9481 shield PCB',(P['tft_pcb_w'],P['tft_pcb_h'],1.6),(tx,ty,tft_pcb_bottom+.8),c=hw_c,m=redpcb,r=1.4),'98 x 64 mm ASSUMED shield outline; header keepout extends 19 mm below PCB.',True)
ref(box('REF | TFT glass',(P['tft_glass_w'],P['tft_glass_h'],P['tft_glass_depth']),(wx,wy,F-.2-P['tft_glass_depth']/2),c=hw_c,m=dark,r=.5),'Fit the measured glass and actual active area before printing.',True)
screen_z=F-.16
ref(box('REF | active LCD',(P['tft_active_w'],P['tft_active_h'],.05),(wx,wy,screen_z),c=hw_c,m=ui_bg),'Illustrative dashboard values, not live readings.',True)
for yy in [ty-24,ty+24]:
    ref(box('REF | shield header block',(57,3,4),(tx,yy,tft_pcb_bottom-2),c=hw_c,m=dark),front=True)
    for xx in range(-53,2,3):ref(cyl('REF | shield pin',.32,7,(xx,yy,tft_pcb_bottom-7),c=hw_c,m=gold,verts=12),front=True)
    ref(box('REF | female jumper envelope',(57,3.6,10),(tx,yy,tft_pcb_bottom-13.8),c=hw_c,m=dark),front=True)

# Dashboard is an editable presentation layer, matching the four documented quantities.
text3('UI title','CLASSROOM MONITOR',(wx-34,wy+20,screen_z+.05),2.65,white,hw_c,front=True)
text3('UI status','WI-FI  /  MQTT',(wx-34,wy+16,screen_z+.05),1.45,green,hw_c,front=True)
for ix,iy,label,val,unit,col in [(-17,4,'TEMPERATURE','26.4','C',green),(18,4,'HUMIDITY','58','%',blue),(-17,-14,'LIGHT','412','lx',amber),(18,-14,'SOUND','52','dB rel.',green)]:
    q=rounded('REF | UI card',33,16,1,screen_z+.025,screen_z+.06,wx+ix,wy+iy,c=hw_c,m=ui_card);ref(q,front=True)
    text3('UI label',label,(wx+ix-14,wy+iy+4,screen_z+.09),1.45,muted,hw_c,front=True)
    text3('UI reading',val,(wx+ix-14,wy+iy-3,screen_z+.09),4.6,white,hw_c,front=True)
    text3('UI units',unit,(wx+ix+7,wy+iy-3,screen_z+.09),1.8,col,hw_c,front=True)
    ref(box('REF | UI bar',(26,0.65,.03),(wx+ix,wy+iy-6,screen_z+.10),c=hw_c,m=col),front=True)

for k,(bw,bh) in P['sensor_boards'].items():
    cy=P['sensor_y'][k];z=P['sensor_board_bottom'][k];ox,oy=P['sensor_xy_offsets'][k]
    ref(box('REF | '+k+' board',(bw,bh,1.6),(sx+ox,cy+oy,z+.8),c=hw_c,m=pcb,r=.7),'Assumed breakout footprint; sensing element placement must be measured.',True)
    if k=='MAX4466':
        ref(cyl('REF | electret capsule',4.8,5.5,(sx,cy,z+4.35),c=hw_c,m=metal),'Align capsule with 8 mm sound port; fit soft foam gasket.',True)
        ref(cyl('REF | microphone mesh',3.7,.1,(sx,cy,z+7.15),c=hw_c,m=dark),front=True)
    else:
        ref(box('REF | '+k+' sensor IC',(4,3,1.1),(sx,cy,z+2.15),c=hw_c,m=dark),front=True)
ref(cyl('REF | piezo buzzer',6,9,(bzx,bzy,F-5),c=hw_c,m=dark),'Nominal 12 mm buzzer; foam tape secures it in the 13.4 mm cup.',True)

for x,y in case_screws:
    ref(cyl('REF | M3 countersunk head',1.65,1.55,(x,y,D-.775),c=hw_c,m=metal,r2=3.15),'M3 x 10 countersunk, four required.',True)
    ref(cyl('REF | brass M3 insert',2.07,5.7,(x,y,T-2.85),c=hw_c,m=gold),'Check insert OD against 4.2 mm pilot; choose length <= 5.7 mm.')

# Surface legends are visual only, so STL users can print in one material.
text3('Product title','CR / MONITOR',(-76,-29,D+.02),4.3,ink,front=True)
text3('Product subtitle','CLASSROOM ENVIRONMENT',(-76,-35,D+.02),1.8,ink,front=True)
text3('Product mark','S3  /  THREE SENSOR NODE',(-76,-53,D+.02),1.65,ink,front=True)
for body,y in [('LIGHT',54),('SOUND',12),('TEMP / RH',-20)]:text3('Sensor legend',body,(sx,y,D+.02),2.05,ink,align='CENTER',front=True)
text3('Buzzer legend','ALARM',(bzx,-56,D+.02),1.7,ink,align='CENTER',front=True)
text3('Revision legend','REV A',(65,-60,D+.02),1.5,ink,align='CENTER',front=True)

# Kept-out spaces are selectable wire objects, never exported as printable parts.
envelope('FIT | shield + connected headers',(P['tft_pcb_w'],P['tft_pcb_h'],P['tft_wired_depth_below_pcb']+1.6),(tx,ty,tft_pcb_bottom+(1.6-P['tft_wired_depth_below_pcb'])/2),'Verify actual female connector depth and wire bend space.')
envelope('FIT | controller with jumpers',(67,33,19.1),(-55,30,17.45),'Wire envelope top Z=27 mm. Display header envelope starts Z=33.6 mm.')
envelope('FIT | purchased holder',(holder_w,holder_h,P['battery_holder_d']),(bx,by,7.4+P['battery_holder_d']/2),'Holder top Z=30.4 mm, below shield header envelope.')
envelope('FIT | power electronics',(42,32,20),(P['power_center_x'],28,17.9),'Envelope only. Charging/protection/5 V regulation must be selected and verified.')

# Printable STL export is independent of the reference and render geometry.
def mesh_triangles(o):
    dg=bpy.context.evaluated_depsgraph_get();ev=o.evaluated_get(dg);me=ev.to_mesh();me.calc_loop_triangles()
    tris=[]
    for f in me.loop_triangles:tris.append([ev.matrix_world@me.vertices[i].co for i in f.vertices])
    ev.to_mesh_clear();return tris
def write_stl(o,path,flip=False):
    tris=mesh_triangles(o)
    allpts=[v for t in tris for v in t];lo=Vector(tuple(min(v[i] for v in allpts) for i in range(3)));hi=Vector(tuple(max(v[i] for v in allpts) for i in range(3)))
    center=(lo+hi)/2
    def trans(v):
        if flip:return Vector((v.x-center.x,-(v.y-center.y),hi.z-v.z))
        return Vector((v.x-center.x,v.y-center.y,v.z-lo.z))
    with open(path,'wb') as f:
        f.write(b'CR Monitor rev A | STL coordinates are millimetres'.ljust(80,b' '));f.write(struct.pack('<I',len(tris)))
        for tr in tris:
            vv=[trans(v) for v in tr];n=(vv[1]-vv[0]).cross(vv[2]-vv[0]).normalized()
            f.write(struct.pack('<12fH',*n,*vv[0],*vv[1],*vv[2],0))
    return {'file':path.name,'triangles':len(tris),'dimensions_mm':[round(float(v),3) for v in hi-lo],'print_orientation':'front face down' if flip else 'back/flat surface down'}

export_report=[]
for o,fn in parts:
    export_report.append(write_stl(o,OUT/'STL'/fn,flip=(o==lid)))
print('STL_EXPORTED',len(parts),flush=True)

# Embedded notes and source allow the Blender file to remain self-explanatory.
notes=bpy.data.texts.new('START HERE — design notes')
notes.write('CR MONITOR / REVISION A\n\nEditable dimensioned prototype, NOT physically fit-tested.\nBody: 186 x 136 x 62 mm. STL units: millimetres.\n\n01–03 collections contain printable parts.\n04 electronics and 05 legends are presentation/reference only.\n06 contains hidden fit envelopes.\nSelect cameras to inspect views. Hide 02 front bezel and front-mounted hardware to inspect the tray.\n\nThe docs identify SHT3x, BH1750, MAX4466, ESP32-S3 DevKitC-1 N16R8, ILI9481 3.5-inch shield and a buzzer.\nThey do not give mechanical drawings, exact breakout variants, battery holder or battery power circuit.\nThe visible boards are explicit assumed envelopes.\n\nRead the included README.md and parameters.json before printing.\nMeasure TFT PCB/glass/active window, headers, DevKit USB positions, sensor boards, and battery holder.\nUse a purchased insulated holder; the cradle does not provide electrical contacts.\nChoose the battery topology, protection, charger and regulated 5 V supply before final assembly.\n\nSource: https://reyvanair.github.io/CR_Monitor/hardware.html\n')
if '__file__' in globals():
    source=bpy.data.texts.new('build_enclosure.py');source.write(Path(__file__).read_text())

# Camera/studio. Modeling units remain millimetres.
def camera(name,pos,target,scale):
    cv=bpy.data.cameras.new(name);o=bpy.data.objects.new(name,cv);studio_c.objects.link(o)
    o.location=pos;o.rotation_euler=(Vector(target)-o.location).to_track_quat('-Z','Y').to_euler()
    cv.type='ORTHO';cv.ortho_scale=scale;cv.clip_start=.1;cv.clip_end=3000
    return o
hero=camera('CAM 01 | assembled three-quarter',(220,-200,330),(0,0,28),310)
front_cam=camera('CAM 02 | front',(0,0,480),(0,0,25),226)
inside_cam=camera('CAM 03 | rear internals',(0,0,500),(0,0,18),218)
explode_cam=camera('CAM 04 | exploded',(230,-290,330),(0,0,70),380)
rear_cam=camera('CAM 05 | rear wall mount',(-190,150,-330),(0,0,20),250)
def area(name,pos,energy,size):
    d=bpy.data.lights.new(name,'AREA');d.energy=energy;d.shape='DISK';d.size=size
    o=bpy.data.objects.new(name,d);studio_c.objects.link(o);o.location=pos;o.rotation_euler=(-o.location).to_track_quat('-Z','Y').to_euler()
area('Large softbox',(-150,180,360),400000,270)
area('Right fill',(240,-70,220),260000,220)
area('Rim light',(-180,-130,120),160000,170)
world=bpy.data.worlds.new('Warm studio');scene.world=world;world.use_nodes=True
world.node_tree.nodes['Background'].inputs[0].default_value=(.62,.68,.73,1)
world.node_tree.nodes['Background'].inputs[1].default_value=.32
scene.render.engine='CYCLES';scene.cycles.samples=P['render_samples'];scene.cycles.use_denoising=True
scene.cycles.device='CPU';scene.render.threads_mode='FIXED';scene.render.threads=8
scene.render.resolution_x=1600;scene.render.resolution_y=1200;scene.render.resolution_percentage=100
scene.render.image_settings.file_format='PNG';scene.render.film_transparent=True
scene.view_settings.view_transform='AgX'
scene.camera=hero
scene['Parameter JSON']=json.dumps(P)
Path(OUT/'parameters.json').write_text(json.dumps(P,indent=2))
Path(OUT/'export_report.json').write_text(json.dumps(export_report,indent=2))
temp_c.hide_render=True
# Default viewport shows the assembled design in material colors, with no studio clutter.
for obj in studio_c.objects:obj.hide_set(True)
for screen in bpy.data.screens:
    for a in screen.areas:
        if a.type=='VIEW_3D':
            a.spaces.active.clip_end=3000;a.spaces.active.clip_start=.1
            a.spaces.active.region_3d.view_distance=290
            a.spaces.active.region_3d.view_location=(0,0,25)
            a.spaces.active.region_3d.view_rotation=hero.rotation_euler.to_quaternion()
            a.spaces.active.shading.color_type='MATERIAL'
active(lid)
# Timeline is a reusable exploded-view control inside Blender.
scene.frame_start=1;scene.frame_end=80
scene.timeline_markers.new('ASSEMBLED',frame=1).camera=hero
scene.timeline_markers.new('EXPLODED',frame=80).camera=explode_cam
for o in front_group:
    dz=110 if o.name.startswith('REF | M3 countersunk head') else (94 if o==lid or o.name in label_c.objects else 47)
    o.keyframe_insert(data_path='location',frame=1)
    o.location.z+=dz;o.keyframe_insert(data_path='location',frame=80)
    o.location.z-=dz
scene.frame_set(1)
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'CR_Monitor_Enclosure.blend'))

if P['render']:
    def render(cam,name):
        bindings=[(marker,marker.camera) for marker in scene.timeline_markers]
        for marker,_ in bindings:marker.camera=None
        scene.camera=cam;scene.render.filepath=str(OUT/'previews'/name)
        bpy.ops.render.render(write_still=True)
        for marker,bound in bindings:marker.camera=bound
        print('RENDERED',name,flush=True)
    render(hero,'01_assembled.png')
    render(front_cam,'02_front.png')
    # Remove all front-attached objects, retain rear tray and installed rear hardware.
    for o in front_group:o.hide_render=True
    render(inside_cam,'03_internal_layout.png')
    for o in front_group:o.hide_render=False
    # Exploded hierarchy: rear fixed, display/mounts lifted, face moved farther forward.
    scene.frame_set(80)
    render(explode_cam,'04_exploded.png')
    scene.frame_set(1)
    scene.camera=hero

print('COMPLETE',str(OUT/'CR_Monitor_Enclosure.blend'),flush=True)
