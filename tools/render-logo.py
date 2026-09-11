#!/usr/bin/env python3
# tools/render-logo.py
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Exsecutor authors.
# ---------------------------------------------------------------------------
# Renders logo/exsecutor-logo.{png,jpg,gif} from the OBJ in logo/. A textured
# software rasteriser -- z-buffered, backface-culled, Lambert plus a rim term,
# 2x supersampled -- because the model is 1,493 vertices and putting Blender
# (3.1 GiB) in reach of this repository for three images would be absurd.
#
# VERIFICATION-ONLY, like tools/ucd-gen/ and tools/gen-codes.py: never on the
# build path, never linked into exsc, and the images it makes are committed
# like any other source file. Deterministic: no clock, no randomness, no
# dict-order dependence; running it twice writes identical bytes.
#
# Run from the repository root: python3 tools/render-logo.py
# ---------------------------------------------------------------------------
import numpy as np, sys
from PIL import Image

OBJ='logo/Meshy_AI_Crossed_Ascension_0910024256_texture.obj'
TEX='logo/Meshy_AI_Crossed_Ascension_0910024256_texture.png'

V=[];VT=[];VN=[];F=[]
for ln in open(OBJ):
    p=ln.split()
    if not p: continue
    if p[0]=='v': V.append([float(x) for x in p[1:4]])
    elif p[0]=='vt': VT.append([float(x) for x in p[1:3]])
    elif p[0]=='vn': VN.append([float(x) for x in p[1:4]])
    elif p[0]=='f':
        idx=[]
        for tok in p[1:]:
            a=(tok.split('/')+['',''])[:3]
            idx.append((int(a[0])-1, int(a[1])-1 if a[1] else -1, int(a[2])-1 if a[2] else -1))
        for i in range(1,len(idx)-1): F.append([idx[0],idx[i],idx[i+1]])
V=np.array(V,dtype=np.float64); VT=np.array(VT,dtype=np.float64) if VT else np.zeros((1,2))
VN=np.array(VN,dtype=np.float64) if VN else np.zeros((1,3))
F=np.array(F,dtype=np.int64)

# centre and scale to unit
c=(V.max(0)+V.min(0))/2; V=V-c
V=V/np.abs(V).max()

tex=np.asarray(Image.open(TEX).convert('RGB'),dtype=np.float64)/255.0
TH,TW=tex.shape[:2]

def render(yaw, S=1024, ss=2, bg=(0x0f,0x12,0x16), pitch=-0.18):
    W=H=S*ss
    cy,sy=np.cos(yaw),np.sin(yaw); cp,sp=np.cos(pitch),np.sin(pitch)
    Ry=np.array([[cy,0,sy],[0,1,0],[-sy,0,cy]])
    Rx=np.array([[1,0,0],[0,cp,-sp],[0,sp,cp]])
    R=Rx@Ry
    P=V@R.T; N=VN@R.T
    # orthographic-ish with slight perspective
    d=3.2
    z=P[:,2]+d
    f=2.0
    x=P[:,0]*f/z; y=P[:,1]*f/z
    sx=(x*0.5+0.5)*W; sy_=(0.5-y*0.5)*H
    scr=np.stack([sx,sy_,z],1)

    img=np.zeros((H,W,3)); img[:]=np.array(bg)/255.0
    zbuf=np.full((H,W),1e9)
    alpha=np.zeros((H,W))
    L=np.array([-0.35,0.55,0.75]); L=L/np.linalg.norm(L)

    for tri in F:
        vi=tri[:,0]; ti=tri[:,1]; ni=tri[:,2]
        p=scr[vi]
        minx=max(int(np.floor(p[:,0].min())),0); maxx=min(int(np.ceil(p[:,0].max())),W-1)
        miny=max(int(np.floor(p[:,1].min())),0); maxy=min(int(np.ceil(p[:,1].max())),H-1)
        if minx>maxx or miny>maxy: continue
        x0,y0=p[0,0],p[0,1]; x1,y1=p[1,0],p[1,1]; x2,y2=p[2,0],p[2,1]
        area=(x1-x0)*(y2-y0)-(x2-x0)*(y1-y0)
        if abs(area)<1e-12: continue
        if area>0: continue   # backface
        yy,xx=np.mgrid[miny:maxy+1, minx:maxx+1]
        px=xx+0.5; py=yy+0.5
        w0=((x1-px)*(y2-py)-(x2-px)*(y1-py))/area
        w1=((x2-px)*(y0-py)-(x0-px)*(y2-py))/area
        w2=1.0-w0-w1
        m=(w0>=0)&(w1>=0)&(w2>=0)
        if not m.any(): continue
        zt=w0*p[0,2]+w1*p[1,2]+w2*p[2,2]
        sub=zbuf[miny:maxy+1, minx:maxx+1]
        m&= zt<sub
        if not m.any(): continue
        # texture
        if ti[0]>=0:
            uv=w0[...,None]*VT[ti[0]]+w1[...,None]*VT[ti[1]]+w2[...,None]*VT[ti[2]]
            u=np.clip((uv[...,0]%1.0)*(TW-1),0,TW-1).astype(np.int32)
            vv=np.clip(((1-uv[...,1])%1.0)*(TH-1),0,TH-1).astype(np.int32)
            base=tex[vv,u]
        else:
            base=np.ones(m.shape+(3,))*0.7
        if ni[0]>=0:
            nrm=w0[...,None]*N[ni[0]]+w1[...,None]*N[ni[1]]+w2[...,None]*N[ni[2]]
            nl=np.linalg.norm(nrm,axis=-1,keepdims=True); nl[nl==0]=1; nrm=nrm/nl
            lam=np.clip((nrm*L).sum(-1),0,1)
            rim=np.clip(1.0-np.abs(nrm[...,2]),0,1)**2.5
        else:
            lam=np.ones(m.shape); rim=np.zeros(m.shape)
        shade=(0.30+0.78*lam)[...,None]*base + (rim*0.42)[...,None]*np.array([0.45,0.62,0.85])
        tgt=img[miny:maxy+1, minx:maxx+1]
        tgt[m]=np.clip(shade[m],0,1)
        sub[m]=zt[m]
        alpha[miny:maxy+1, minx:maxx+1][m]=1.0
    out=Image.fromarray((np.clip(img,0,1)*255).astype(np.uint8)).resize((S,S),Image.LANCZOS)
    a=Image.fromarray((alpha*255).astype(np.uint8)).resize((S,S),Image.LANCZOS)
    return out,a

if __name__=='__main__':
    import math
    rgb,a = render(0.16, S=1024, ss=2, pitch=-0.14)
    png = rgb.convert('RGBA'); png.putalpha(a)
    png.save('logo/exsecutor-logo.png', optimize=True)

    hero,_ = render(0.16, S=1200, ss=2, pitch=-0.14)
    hero.convert('RGB').save('logo/exsecutor-logo.jpg', quality=94,
                             optimize=True, progressive=True)

    # Not a turntable: an extruded flat mark goes edge-on at 90 degrees and
    # disappears. It rocks instead, and is readable in every frame. No
    # dithering -- flat shading on a flat ground quantises cleanly at 64
    # colours, and dither was what made an earlier version big and noisy.
    N = 28
    frames = []
    for i in range(N):
        t = i / N
        yaw = 0.45 * math.sin(2*math.pi*t)
        pit = -0.14 + 0.05 * math.cos(2*math.pi*t)
        im,_ = render(yaw, S=400, ss=2, pitch=pit)
        frames.append(im.convert('RGB'))
    pal = frames[0].quantize(colors=64, method=Image.MEDIANCUT, dither=Image.NONE)
    q = [f.quantize(palette=pal, dither=Image.NONE) for f in frames]
    q[0].save('logo/exsecutor-logo.gif', save_all=True, append_images=q[1:],
              duration=80, loop=0, optimize=True, disposal=2)
    print('wrote logo/exsecutor-logo.{png,jpg,gif}; '
          'then: gifsicle -O3 logo/exsecutor-logo.gif -o logo/exsecutor-logo.gif')
