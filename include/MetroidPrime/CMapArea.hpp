#ifndef _CMAPAREA
#define _CMAPAREA

#include "types.h"

#include "MetroidPrime/CMappableObject.hpp"

#include "Kyoto/Math/CAABox.hpp"
#include "Kyoto/Math/CTransform4f.hpp"
#include "rstl/single_ptr.hpp"

class CInputStream;
class CMapWorldInfo;
class CColor;
class IWorld;

class CMapArea {
public:
  class CMapAreaSurface {
    friend class CMapArea;

  public:
    static void SetupGXMaterial();
    void Draw(const CVector3f* verts, const CColor& surfColor, const CColor& lineColor,
              float lineWidth) const;
    void PostConstruct(const void* buf);
    CVector3f GetNormal() const { return mNormal; }
    CVector3f GetCenterPosition() const { return mCentroid; }
#ifdef TARGET_PC
    // The surface's primitive list (count, then type, vertex count and 8-bit vertex indices each).
    const int* PortGetSurfaceList() const { return mSurfOffset; }
#endif

  private:
    CVector3f mNormal;
    CVector3f mCentroid;
    const int* mSurfOffset;
    const int* mOutlineOffset;
#ifdef TARGET_PC
    // Byte size of the owning area's vertex array, which Draw uploads natively (see CMapArea.cpp).
    uint mVertexArraySize;
#endif
  };

  enum EVisMode { kVM_Always, kVM_MapStationOrVisit, kVM_Visit, kVM_Never };

  CMapArea(CInputStream& in, uint size);
  ~CMapArea();

  void PostConstruct();
  void BuildDisplayLists(); // Guessed name
#ifdef TARGET_PC
  static void BuildHostSurface(CMapAreaSurface& surf, const uchar* rec, uchar* buf);
  bool PortIsNeverMapped() const { return mVisibilityMode == kVM_Never; }
  int PortGetNumVertices() const { return mVertexCount; }
#endif
  bool GetIsVisibleToAutoMapper(bool worldVis, bool areaVis) const;
  bool IsInDarkWorld() const; // Guessed name
  CVector3f GetAreaCenterPoint() const;
  CTransform4f GetAreaPostTransform(const IWorld& world, int areaId);
  const CVector3f& GetAreaPostTranslate(const IWorld& world);
  // Guessed names for the prepared surface/outline/door rendering path.
  static void SetupLighting(const CTransform4f& cameraXf);
  void Draw(const CColor& surfaceColor, const CColor& outlineColor, const CTransform4f& areaXf,
            const CTransform4f& modelXf, int selArea, const CMapWorldInfo& info, float alpha) const;
  const CAABox& GetBoundingBox() const { return mBox; }
  const CVector3f& GetMapAdjustment() const { return mMapAdjustment; } // Guessed name
  int GetNumMappableObjects() const { return mMappableObjCount; }
  const CMappableObject& GetMappableObject(int idx) const { return mMoStart[idx]; }
  int GetNumSurfaces() const { return mSurfaceCount; }
  const CMapAreaSurface& GetSurface(int idx) const { return mSurfaceStart[idx]; }
  const CVector3f* GetVertices() const { return mVertexStart; }

private:
  uint mMagic;
  uint mVersion;
  uint mFlags; // Guessed name; bit 0 identifies a Dark Aether map area.
  EVisMode mVisibilityMode;
  CAABox mBox;
  CVector3f mMapAdjustment; // Guessed name
  int mMappableObjCount;
  int mVertexCount;
  int mSurfaceCount;
  uint mSize;
  CMappableObject* mMoStart;
  CVector3f* mVertexStart;
  CMapAreaSurface* mSurfaceStart;
  rstl::single_ptr< uchar > mBuf;
#ifdef TARGET_PC
  // Host CMapAreaSurface holds pointers and outgrows the 0x20-byte disc records, so the
  // surfaces live in their own buffer instead of in place in mBuf.
  rstl::single_ptr< uchar > mHostSurfaces;
#endif

  // Guessed names: prepared rendering storage and its non-owning sections.
  rstl::single_ptr< uchar > mRenderBuf;
  uint mRenderBufSize;
  uchar* mDoorVertices;
  uint mDoorVerticesSize;
  uchar* mSurfaceNormals;
  uint mSurfaceNormalsSize;
  uchar* mSurfaceDisplayList;
  uint mSurfaceDisplayListSize;
  uchar* mOutlineDisplayList;
  uint mOutlineDisplayListSize;
};
NESTED_CHECK_SIZEOF(CMapArea, CMapAreaSurface, 0x20)
CHECK_SIZEOF(CMapArea, 0x7c)

#endif // _CMAPAREA
