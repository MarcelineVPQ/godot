// Per-entry material access. Include right after geometries[] and materials[] are declared;
// every read of materials[] goes through these.

// Geometry entry of a hit on geometry p_geometry_index of the TLAS instance with custom index p_custom_index.
// A BLAS built from clusters reports the cluster as the geometry index, and has a single entry.
uint rt_geometry_idx(uint p_custom_index, uint p_geometry_index) {
	if (p_geometry_index == 0u || (geometries[p_custom_index].flags & FLAG_CLUSTERS) != 0u) {
		return p_custom_index;
	}
	return p_custom_index + p_geometry_index;
}

// Material of the TLAS geometry entry p_geometry_idx, with the entry's flags and instance transparency applied.
MaterialData rt_entry_material(uint p_geometry_idx) {
	const uint material_index = geometries[p_geometry_idx].material_index;
	MaterialData mat = materials[material_index & RT_MATERIAL_INDEX_MASK];
	mat.albedo_texture |= material_index & ~RT_MATERIAL_INDEX_MASK;
	mat.flags |= geometries[p_geometry_idx].material_flags;
	return mat;
}

// RT_MAT_FLAG_* of the TLAS geometry entry p_geometry_idx.
uint rt_entry_material_flags(uint p_geometry_idx) {
	return materials[geometries[p_geometry_idx].material_index & RT_MATERIAL_INDEX_MASK].flags | geometries[p_geometry_idx].material_flags;
}
