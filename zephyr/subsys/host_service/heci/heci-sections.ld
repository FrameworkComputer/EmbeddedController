SECTION_DATA_PROLOGUE(heci_descriptor,,SUBALIGN(4))
{
	__heci_desc_start = .;
	*(".heci.desc*")
	KEEP(*(SORT_BY_NAME(".heci.desc*")))
	__heci_desc_end = .;
} GROUP_DATA_LINK_IN(RAMABLE_REGION, ROMABLE_REGION)
