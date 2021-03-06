/*---------------------------------------------------------------------------*\
  =========                 |
  \\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox
   \\    /   O peration     |
    \\  /    A nd           | Copyright (C) 2011-2013 OpenFOAM Foundation
     \\/     M anipulation  |
-------------------------------------------------------------------------------
License
    This file is part of OpenFOAM.

    OpenFOAM is free software: you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    OpenFOAM is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
    for more details.

    You should have received a copy of the GNU General Public License
    along with OpenFOAM.  If not, see <http://www.gnu.org/licenses/>.

Application
    createBaffles

Description
    Makes internal faces into boundary faces. Does not duplicate points, unlike
    mergeOrSplitBaffles.

    Note: if any coupled patch face is selected for baffling the opposite
    member has to be selected for baffling as well.

    - if the patch already exists will not override it nor its fields
    - if the patch does not exist it will be created together with 'calculated'
      patchfields unless the field is mentioned in the patchFields section.

\*---------------------------------------------------------------------------*/

#include "argList.H"
#include "Time.H"
#include "polyTopoChange.H"
#include "polyModifyFace.H"
#include "polyAddFace.H"
#include "ReadFields.H"
#include "volFields.H"
#include "surfaceFields.H"
#include "fvMeshMapper.H"
#include "faceSelection.H"

#include "fvMeshTools.H"

using namespace Foam;

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

void modifyOrAddFace
(
    polyTopoChange& meshMod,
    const face& f,
    const label faceI,
    const label own,
    const bool flipFaceFlux,
    const label newPatchI,
    const label zoneID,
    const bool zoneFlip,

    PackedBoolList& modifiedFace
)
{
    if (!modifiedFace[faceI])
    {
        // First usage of face. Modify.
        meshMod.setAction
        (
            polyModifyFace
            (
                f,                          // modified face
                faceI,                      // label of face
                own,                        // owner
                -1,                         // neighbour
                flipFaceFlux,               // face flip
                newPatchI,                  // patch for face
                false,                      // remove from zone
                zoneID,                     // zone for face
                zoneFlip                    // face flip in zone
            )
        );
        modifiedFace[faceI] = 1;
    }
    else
    {
        // Second or more usage of face. Add.
        meshMod.setAction
        (
            polyAddFace
            (
                f,                          // modified face
                own,                        // owner
                -1,                         // neighbour
                -1,                         // master point
                -1,                         // master edge
                faceI,                      // master face
                flipFaceFlux,               // face flip
                newPatchI,                  // patch for face
                zoneID,                     // zone for face
                zoneFlip                    // face flip in zone
            )
        );
    }
}



int main(int argc, char *argv[])
{
    argList::addNote
    (
        "Makes internal faces into boundary faces.\n"
        "Does not duplicate points."
    );
    #include "addDictOption.H"
    #include "addOverwriteOption.H"
    #include "addDictOption.H"
    #include "addRegionOption.H"
    #include "setRootCase.H"
    #include "createTime.H"
    runTime.functionObjects().off();
    #include "createNamedMesh.H"


    const bool overwrite = args.optionFound("overwrite");

    const word oldInstance = mesh.pointsInstance();

    const word dictName("createBafflesDict");
    #include "setSystemMeshDictionaryIO.H"

    Switch internalFacesOnly(false);

    Switch noFields(false);

    PtrList<faceSelection> selectors;
    {
        Info<< "Reading baffle criteria from " << dictName << nl << endl;
        IOdictionary dict(dictIO);

        dict.lookup("internalFacesOnly") >> internalFacesOnly;
        noFields = dict.lookupOrDefault("noFields", false);

        const dictionary& selectionsDict = dict.subDict("baffles");

        label n = 0;
        forAllConstIter(dictionary, selectionsDict, iter)
        {
            if (iter().isDict())
            {
                n++;
            }
        }
        selectors.setSize(n);
        n = 0;
        forAllConstIter(dictionary, selectionsDict, iter)
        {
            if (iter().isDict())
            {
                selectors.set
                (
                    n++,
                    faceSelection::New(iter().keyword(), mesh, iter().dict())
                );
            }
        }
    }


    if (internalFacesOnly)
    {
        Info<< "Not converting faces on non-coupled patches." << nl << endl;
    }


    // Read objects in time directory
    IOobjectList objects(mesh, runTime.timeName());

    // Read vol fields.
    Info<< "Reading geometric fields" << nl << endl;
    PtrList<volScalarField> vsFlds;
    if (!noFields) ReadFields(mesh, objects, vsFlds);

    PtrList<volVectorField> vvFlds;
    if (!noFields) ReadFields(mesh, objects, vvFlds);

    PtrList<volSphericalTensorField> vstFlds;
    if (!noFields) ReadFields(mesh, objects, vstFlds);

    PtrList<volSymmTensorField> vsymtFlds;
    if (!noFields) ReadFields(mesh, objects, vsymtFlds);

    PtrList<volTensorField> vtFlds;
    if (!noFields) ReadFields(mesh, objects, vtFlds);

    // Read surface fields.

    PtrList<surfaceScalarField> ssFlds;
    if (!noFields) ReadFields(mesh, objects, ssFlds);

    PtrList<surfaceVectorField> svFlds;
    if (!noFields) ReadFields(mesh, objects, svFlds);

    PtrList<surfaceSphericalTensorField> sstFlds;
    if (!noFields) ReadFields(mesh, objects, sstFlds);

    PtrList<surfaceSymmTensorField> ssymtFlds;
    if (!noFields) ReadFields(mesh, objects, ssymtFlds);

    PtrList<surfaceTensorField> stFlds;
    if (!noFields) ReadFields(mesh, objects, stFlds);




    // Creating (if necessary) baffles
    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

    forAll(selectors, selectorI)
    {
        const word& name = selectors[selectorI].name();

        if (mesh.faceZones().findZoneID(name) == -1)
        {
            mesh.faceZones().clearAddressing();
            label sz = mesh.faceZones().size();

            labelList addr(0);
            boolList flip(0);
            mesh.faceZones().setSize(sz+1);
            mesh.faceZones().set
            (
                sz,
                new faceZone(name, addr, flip, sz, mesh.faceZones())
            );
        }
    }


    // Select faces
    // ~~~~~~~~~~~~

    //- Per face zoneID it is in and flip status.
    labelList faceToZoneID(mesh.nFaces(), -1);
    boolList faceToFlip(mesh.nFaces(), false);
    forAll(selectors, selectorI)
    {
        const word& name = selectors[selectorI].name();
        label zoneID = mesh.faceZones().findZoneID(name);

        selectors[selectorI].select(zoneID, faceToZoneID, faceToFlip);
    }

    // Add faces to faceZones
    labelList nFaces(mesh.faceZones().size(), 0);
    forAll(faceToZoneID, faceI)
    {
        label zoneID = faceToZoneID[faceI];
        if (zoneID != -1)
        {
            nFaces[zoneID]++;
        }
    }

    forAll(selectors, selectorI)
    {
        const word& name = selectors[selectorI].name();
        label zoneID = mesh.faceZones().findZoneID(name);

        label& n = nFaces[zoneID];
        labelList addr(n);
        boolList flip(n);
        n = 0;
        forAll(faceToZoneID, faceI)
        {
            label zone = faceToZoneID[faceI];
            if (zone == zoneID)
            {
                addr[n] = faceI;
                flip[n] = faceToFlip[faceI];
                n++;
            }
        }

        Info<< "Created zone " << name
            << " at index " << zoneID
            << " with " << n << " faces" << endl;

        mesh.faceZones().set
        (
            zoneID,
            new faceZone(name, addr, flip, zoneID, mesh.faceZones())
        );
    }



    // Count patches to add
    // ~~~~~~~~~~~~~~~~~~~~
    HashSet<word> bafflePatches;
    {
        forAll(selectors, selectorI)
        {
            const dictionary& patchSources
            (
                selectors[selectorI].dict().subDict("patches")
            );
            forAllConstIter(dictionary, patchSources, iter)
            {
                //const word& patchName = iter().keyword();
                const word patchName(iter().dict()["name"]);
                bafflePatches.insert(patchName);
            }
        }
    }



    // Create baffles
    // ~~~~~~~~~~~~~~
    // Is done in multiple steps
    // - create patches with 'calculated' patchFields
    // - move faces into these patches
    // - change the patchFields to the wanted type
    // This order is done so e.g. fixedJump works:
    // - you cannot create patchfields at the same time as patches since
    //   they do an evaluate upon construction
    // - you want to create the patchField only after you have faces
    //   so you don't get the 'create-from-nothing' mapping problem.


    // Pass 1: add patches
    // ~~~~~~~~~~~~~~~~~~~

    //HashSet<word> addedPatches;
    {
        const polyBoundaryMesh& pbm = mesh.boundaryMesh();
        forAll(selectors, selectorI)
        {
            const dictionary& patchSources
            (
                selectors[selectorI].dict().subDict("patches")
            );
            forAllConstIter(dictionary, patchSources, iter)
            {
                //const word& patchName = iter().keyword();
                const word patchName(iter().dict()["name"]);

                label destPatchI = pbm.findPatchID(patchName);

                if (destPatchI == -1)
                {
                    dictionary patchDict = iter().dict();
                    patchDict.set("nFaces", 0);
                    patchDict.set("startFace", 0);

                    Info<< "Adding new patch " << patchName
                        << " from " << patchDict << endl;

                    autoPtr<polyPatch> ppPtr
                    (
                        polyPatch::New
                        (
                            patchName,
                            patchDict,
                            0,
                            pbm
                        )
                    );

                    // Add patch, create calculated everywhere
                    fvMeshTools::addPatch
                    (
                        mesh,
                        ppPtr(),
                        dictionary(),   // do not set specialised patchFields
                        calculatedFvPatchField<scalar>::typeName,
                        true            // parallel sync'ed addition
                    );

                    //addedPatches.insert(patchName);
                }
                else
                {
                    Info<< "Patch '" << patchName << "' already exists.  Only "
                        << "moving patch faces - type will remain the same"
                        << endl;
                }
            }
        }
    }



    // Make sure patches and zoneFaces are synchronised across couples
    mesh.boundaryMesh().checkParallelSync(true);
    mesh.faceZones().checkParallelSync(true);



    // Mesh change container
    polyTopoChange meshMod(mesh);

    const polyBoundaryMesh& pbm = mesh.boundaryMesh();


    // Do the actual changes. Note:
    // - loop in incrementing face order (not necessary if faceZone ordered).
    //   Preserves any existing ordering on patch faces.
    // - two passes, do non-flip faces first and flip faces second. This
    //   guarantees that when e.g. creating a cyclic all faces from one
    //   side come first and faces from the other side next.

    // Whether first use of face (modify) or consecutive (add)
    PackedBoolList modifiedFace(mesh.nFaces());
    label nModified = 0;

    forAll(selectors, selectorI)
    {
        const word& name = selectors[selectorI].name();
        label zoneID = mesh.faceZones().findZoneID(name);
        const faceZone& fZone = mesh.faceZones()[zoneID];

        const dictionary& patchSources
        (
            selectors[selectorI].dict().subDict("patches")
        );

        DynamicList<label> newMasterPatches(patchSources.size());
        DynamicList<label> newSlavePatches(patchSources.size());

        bool master = true;

        forAllConstIter(dictionary, patchSources, iter)
        {
            //const word& patchName = iter().keyword();
            const word patchName(iter().dict()["name"]);
            label patchI = pbm.findPatchID(patchName);
            if (master)
            {
                newMasterPatches.append(patchI);
            }
            else
            {
                newSlavePatches.append(patchI);
            }
            master = !master;
        }



        forAll(newMasterPatches, i)
        {
            // Pass 1. Do selected side of zone
            // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

            for (label faceI = 0; faceI < mesh.nInternalFaces(); faceI++)
            {
                label zoneFaceI = fZone.whichFace(faceI);

                if (zoneFaceI != -1)
                {
                    if (!fZone.flipMap()[zoneFaceI])
                    {
                        // Use owner side of face
                        modifyOrAddFace
                        (
                            meshMod,
                            mesh.faces()[faceI],    // modified face
                            faceI,                  // label of face
                            mesh.faceOwner()[faceI],// owner
                            false,                  // face flip
                            newMasterPatches[i],    // patch for face
                            fZone.index(),          // zone for face
                            false,                  // face flip in zone
                            modifiedFace            // modify or add status
                        );
                    }
                    else
                    {
                        // Use neighbour side of face
                        modifyOrAddFace
                        (
                            meshMod,
                            mesh.faces()[faceI].reverseFace(),  // modified face
                            faceI,                      // label of face
                            mesh.faceNeighbour()[faceI],// owner
                            true,                       // face flip
                            newMasterPatches[i],        // patch for face
                            fZone.index(),              // zone for face
                            true,                       // face flip in zone
                            modifiedFace                // modify or add status
                        );
                    }

                    nModified++;
                }
            }


            // Pass 2. Do other side of zone
            // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

            for (label faceI = 0; faceI < mesh.nInternalFaces(); faceI++)
            {
                label zoneFaceI = fZone.whichFace(faceI);

                if (zoneFaceI != -1)
                {
                    if (!fZone.flipMap()[zoneFaceI])
                    {
                        // Use neighbour side of face
                        modifyOrAddFace
                        (
                            meshMod,
                            mesh.faces()[faceI].reverseFace(),  // modified face
                            faceI,                          // label of face
                            mesh.faceNeighbour()[faceI],    // owner
                            true,                           // face flip
                            newSlavePatches[i],             // patch for face
                            fZone.index(),                  // zone for face
                            true,                           // face flip in zone
                            modifiedFace                    // modify or add
                        );
                    }
                    else
                    {
                        // Use owner side of face
                        modifyOrAddFace
                        (
                            meshMod,
                            mesh.faces()[faceI],    // modified face
                            faceI,                  // label of face
                            mesh.faceOwner()[faceI],// owner
                            false,                  // face flip
                            newSlavePatches[i],     // patch for face
                            fZone.index(),          // zone for face
                            false,                  // face flip in zone
                            modifiedFace            // modify or add status
                        );
                    }
                }
            }


            // Modify any boundary faces
            // ~~~~~~~~~~~~~~~~~~~~~~~~~

            // Normal boundary:
            // - move to new patch. Might already be back-to-back baffle
            // you want to add cyclic to. Do warn though.
            //
            // Processor boundary:
            // - do not move to cyclic
            // - add normal patches though.

            // For warning once per patch.
            labelHashSet patchWarned;

            forAll(pbm, patchI)
            {
                const polyPatch& pp = pbm[patchI];

                label newPatchI = newMasterPatches[i];

                if (pp.coupled() && pbm[newPatchI].coupled())
                {
                    // Do not allow coupled faces to be moved to different
                    // coupled patches.
                }
                else if (pp.coupled() || !internalFacesOnly)
                {
                    forAll(pp, i)
                    {
                        label faceI = pp.start()+i;

                        label zoneFaceI = fZone.whichFace(faceI);

                        if (zoneFaceI != -1)
                        {
                            if (patchWarned.insert(patchI))
                            {
                                WarningIn(args.executable())
                                    << "Found boundary face (in patch "
                                    << pp.name()
                                    << ") in faceZone " << fZone.name()
                                    << " to convert to baffle patch "
                                    << pbm[newPatchI].name()
                                    << endl
                                    << "    Run with -internalFacesOnly option"
                                    << " if you don't wish to convert"
                                    << " boundary faces." << endl;
                            }

                            modifyOrAddFace
                            (
                                meshMod,
                                mesh.faces()[faceI],        // modified face
                                faceI,                      // label of face
                                mesh.faceOwner()[faceI],    // owner
                                false,                      // face flip
                                newPatchI,                  // patch for face
                                fZone.index(),              // zone for face
                                fZone.flipMap()[zoneFaceI], // face flip in zone
                                modifiedFace                // modify or add
                            );
                            nModified++;
                        }
                    }
                }
            }
        }
    }


    Info<< "Converted " << returnReduce(nModified, sumOp<label>())
        << " faces into boundary faces in patches "
        << bafflePatches.sortedToc() << nl << endl;

    if (!overwrite)
    {
        runTime++;
    }

    // Change the mesh. Change points directly (no inflation).
    autoPtr<mapPolyMesh> map = meshMod.changeMesh(mesh, false);

    // Update fields
    mesh.updateMesh(map);



    // Correct boundary faces mapped-out-of-nothing.
    // This is just a hack to correct the value field.
    {
        fvMeshMapper mapper(mesh, map);
        bool hasWarned = false;

        forAllConstIter(HashSet<word>, bafflePatches, iter)
        {
            label patchI = mesh.boundaryMesh().findPatchID(iter.key());

            const fvPatchMapper& pm = mapper.boundaryMap()[patchI];

            if (pm.sizeBeforeMapping() == 0)
            {
                if (!hasWarned)
                {
                    hasWarned = true;
                    WarningIn(args.executable())
                        << "Setting field on boundary faces to zero." << endl
                        << "You might have to edit these fields." << endl;
                }

                fvMeshTools::zeroPatchFields(mesh, patchI);
            }
        }
    }


    // Pass 2: change patchFields
    // ~~~~~~~~~~~~~~~~~~~~~~~~~~

    {
        const polyBoundaryMesh& pbm = mesh.boundaryMesh();
        forAll(selectors, selectorI)
        {
            const dictionary& patchSources
            (
                selectors[selectorI].dict().subDict("patches")
            );
            forAllConstIter(dictionary, patchSources, iter)
            {
                //const word& patchName = iter().keyword();
                const word patchName(iter().dict()["name"]);
                label patchI = pbm.findPatchID(patchName);

                if (iter().dict().found("patchFields"))
                {
                    const dictionary& patchFieldsDict = iter().dict().subDict
                    (
                        "patchFields"
                    );

                    fvMeshTools::setPatchFields
                    (
                        mesh,
                        patchI,
                        patchFieldsDict
                    );
                }
            }
        }
    }


    // Move mesh (since morphing might not do this)
    if (map().hasMotionPoints())
    {
        mesh.movePoints(map().preMotionPoints());
    }

    if (overwrite)
    {
        mesh.setInstance(oldInstance);
    }

    Info<< "Writing mesh to " << runTime.timeName() << endl;

    mesh.write();

    Info<< "End\n" << endl;

    return 0;
}


// ************************************************************************* //
