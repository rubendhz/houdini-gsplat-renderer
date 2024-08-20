# houdini-gsplat-renderer
A HDK/GLSL implementation of Gaussian Splatting

[![Watch the video](https://i.vimeocdn.com/video/1851733134-68364e97e7700b64d42eb89669d26a56027175614256171f47b983ad7da5fa4f-d?f=webp)](https://vimeo.com/945995885)  
[Watch the video](https://vimeo.com/945995885)

https://www.sidefx.com/docs/hdk/_h_d_k__intro__creating_plugins.html
https://www.sidefx.com/docs/hdk/_h_d_k__intro__compiling.html

# To-Do

NEW FEATURES:
- [ ] Debug modes through detail attribs?
- [ ] Enable gsplat geo modifications a posteriori
- [ ] ShaderManager improvements, incl. multi render contexts
- [ ] Spherical harmonics rotation / Xform SOP
- [ ] GSplat environment as IBL
- [ ] Integration with other CG
- [ ] RenderToTexture, and/or ROP/COP nodes? Overlap with other projects!

PERFORMANCE:
- [ ] View frustum culling
- [ ] investigate slowdown past 2.1 mill splats
- [ ] Sorting via shader compute
- [ ] Fast time-dependency
- [ ] Move away from textures in favour of VAOs

IMPROVEMENTS:
- [ ] Cleanup hipfile
- [ ] More elegant orient correction fix in shader
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
