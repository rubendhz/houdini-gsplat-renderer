# houdini-gsplat-renderer
A HDK/GLSL implementation of Gaussian Splatting

# MVP To-Do

TIER 1
- [ ] Clenup hipfile
- [ ] Compatible with GSOPs
- [ ] ShaderManager improvements, incl. multi render contexts (+cleanup)
- [ ] Debug modes through detail attribs?
- [ ] Pointer safety audit
- [ ] View frustum culling
- [ ] mega-slowdown past 2.1 mill

TIER 2
- [ ] Picking render mode
- [ ] Spherical harmonics rotation
- [ ] Move away from textures in favour of VAOs
- [ ] Code cleanup


# Beyond MVP To-Do
- [ ] Better orient fix in shader
- [ ] Integration with other CG
- [ ] Viewport options exposed in UI
- [ ] RenderToTexture, and/or ROP/COP nodes
- [ ] Sorting via shader compute
- [ ] VSCode relying on env variables & multi-platform launch configs









For reference, one can add these lines to .bashrc (or equivalent) to define a simple dev workflow, triggered by just doing `gsplat_dev` from terminal

```
export HOUDINI_VERSION="19.5.368" # or whatever version is relevant
henv()
{
    local current_path="$(pwd)"
    cd "/opt/hfs${HOUDINI_VERSION}" || return
    source houdini_setup
    cd "$current_dir" || return
}
gsplat_dev()
{
    henv
    cd ~/dev/repos/personal/houdini-gsplat-renderer || return
    cd gsplat_plugin || return
    hcustom gsplat.C -I include -I shaders || return
    cd ..
    #mv -f gsplat_plugin/gsplat.o gsplat.o
    h195 hip/gplat_dev_v1.hip
}
```
