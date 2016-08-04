#include "HeatMapNode.h"

#include <base/ScopeTimer.h>
#include <graphics/VertexArray.h>
#include <graphics/GLContextManager.h>
#include <graphics/Bitmap.h>
#include <graphics/Color.h>
#include <player/Arg.h>
#include <player/TypeDefinition.h>
#include <player/TypeRegistry.h>
#include <player/OGLSurface.h>

using namespace boost;
using namespace std;
using namespace avg;

bool SHOULD_PRERENDER = false;

void HeatMapNode::registerType()
{
    vector<string> cm;
    TypeDefinition def = TypeDefinition("heatmapnode", "rasternode",  ExportedObject::buildObject<HeatMapNode>)
        .addArg( Arg<glm::vec2>("viewportrangemin", glm::vec2(0,0), false, offsetof(HeatMapNode, m_ViewportRangeMin)) )
        .addArg( Arg<glm::vec2>("viewportrangemax", glm::vec2(0,0), false, offsetof(HeatMapNode, m_ViewportRangeMax)) )
        .addArg( Arg<glm::vec2>("mapsize", glm::vec2(0,0), false, offsetof(HeatMapNode, m_MapSize)) )
        .addArg( Arg<float >("valuerangemin", 0.0, false, offsetof(HeatMapNode, m_ValueRangeMin)) )
        .addArg( Arg<float >("valuerangemax", 0.0, false, offsetof(HeatMapNode, m_ValueRangeMax)) )
        .addArg( Arg<vector<string> >("colormap", cm, false, offsetof(HeatMapNode, m_ColorMap)) )
        ;

    const char* allowedParentNodeNames[] = {"div", "canvas", "avg", 0};
    TypeRegistry::get()->registerType(def, allowedParentNodeNames);
}

HeatMapNode::HeatMapNode(const ArgList& args, const string& sPublisherName) : RasterNode(sPublisherName)
{
    args.setMembers(this);
    setSize(args.getArgVal<glm::vec2>("size"));
    createColorRange(m_ValueRangeMin, m_ValueRangeMax);
}

HeatMapNode::~HeatMapNode() {}

void HeatMapNode::connect(CanvasPtr pCanvas)
{
    RasterNode::connect(pCanvas);
}

void HeatMapNode::connectDisplay()
{
    if (m_Matrix.size() != 0)
    {
        m_pTex = GLContextManager::get()->createTexture(glm::vec2(16,16), R8G8B8A8, getMipmap());
        getSurface()->create(R8G8B8A8, m_pTex);
        setupFX();
        RasterNode::connectDisplay();
    }
}

void HeatMapNode::disconnect(bool bKill)
{
    RasterNode::disconnect(bKill);
}

static ProfilingZoneID PrerenderProfilingZone("HeatMapNode::prerender");
void HeatMapNode::preRender(const VertexArrayPtr& pVA, bool bIsParentActive, float parentEffectiveOpacity)
{
    if (m_pTex && SHOULD_PRERENDER)
    {
        ScopeTimer timer(PrerenderProfilingZone);
        AreaNode::preRender(pVA, bIsParentActive, parentEffectiveOpacity);

        BitmapPtr pBmp(new Bitmap(glm::vec2(m_Matrix.size(),m_Matrix.front().size()), R8G8B8A8));

        int Stride = pBmp->getStride()/pBmp->getBytesPerPixel();
        IntPoint size = pBmp->getSize();
        Pixel32 * pLine = (Pixel32*)(pBmp->getPixels());
        Pixel32 * pPixel;
        for (int y=0; y<size.y; ++y) {
            pPixel = pLine;
            for (int x=0; x<size.x; ++x) {
                avg::Pixel32 c;
                std::map<float, avg::Pixel32>::iterator low, prev;
                double pos = m_Matrix[x][y];
                low = m_ColorMapping.lower_bound(pos);
                if (low == m_ColorMapping.end()) {
                    // nothing found
                } else if (low == m_ColorMapping.begin()) {
                    c = m_ColorMapping[low->first];
                } else {
                    prev = low;
                    --prev;
                    if ((pos - prev->first) < (low->first - pos)) {
                        c = m_ColorMapping[prev->first];
                    } else {
                        c = m_ColorMapping[low->first];
                    }
                }
                *pPixel = c;
                pPixel++;
            }
            pLine += Stride;
        }
        GLContextManager::get()->scheduleTexUpload(m_pTex, pBmp);
        scheduleFXRender();
        calcVertexArray(pVA);

        SHOULD_PRERENDER = false;
    }
}

static ProfilingZoneID RenderProfilingZone("HeatMapNode::render");
void HeatMapNode::render(GLContext* pContext, const glm::mat4& transform)
{
    ScopeTimer Timer(RenderProfilingZone);
    blt32(pContext, transform);
}

void HeatMapNode::setSize(const glm::vec2& pt)
{
    m_Size = pt;
}

const vector<string>& HeatMapNode::getColorMap() const
{
    return m_ColorMap;
}

void HeatMapNode::setColorMap(const vector<string>& colormap)
{
    m_ColorMap = colormap;
}

void HeatMapNode::setPosns(const std::vector<glm::vec2>& posns)
{
  cout << "POSNS SET" << endl;
  SHOULD_PRERENDER = true;
}

void HeatMapNode::setMatrix(const vector<vector<float> >& matrix)
{
    m_Matrix = matrix;

    if (!m_pTex && m_Matrix.size() != 0)
    {
        m_pTex = GLContextManager::get()->createTexture(glm::vec2(m_Matrix.size(),m_Matrix.front().size()), R8G8B8A8, getMipmap());
        getSurface()->create(R8G8B8A8, m_pTex);
        setupFX();
        RasterNode::connectDisplay();
    }

    SHOULD_PRERENDER = true;
}

void HeatMapNode::createColorRange(const float& min, const float& max)
{
    float _min;
    float _max;
    float goes_over_zero = 0;
    float is_nonchanging_range = 0;
    if (min < 0)
        _min = -min;
    else
        _min = min;

    if (max < 0)
        _max = -max;
    else
        _max = max;

    if (min < 0 && max > 0)
        goes_over_zero = 1;
    else
        is_nonchanging_range = 1;

    float range_size = _min + _max - is_nonchanging_range;
    float range_steps = range_size / (m_ColorMap.size() - goes_over_zero);

    for (int i=0; i < m_ColorMap.size(); ++i)
    {
      float v = max - (i*range_steps);
      //cout << v << " # " << m_ColorMap.at( (m_ColorMap.size()-1) - i) << endl;
      m_ColorMapping[v] = Color(m_ColorMap.at( (m_ColorMap.size()-1) - i));
    }
}
