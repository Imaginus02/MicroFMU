#include "py/runtime.h"


// Define a function :
static mp_obj_t example_multiply_ints(mp_obj_t a_obj, mp_obj_t b_obj) {
	int a = mp_obj_get_int(a_obj);
	int b = mp_obj_get_int(b_obj);

	return mp_obj_new_int(a*b);
}

// On permet l'appel de cette fonction dans python :
static MP_DEFINE_CONST_FUN_OBJ_2(example_multiply_ints_obj, example_multiply_ints);

// Définition d'une class :
typedef struct _example_Room_obj_t {
	
	//Objet "base" obligatoire au début
	mp_obj_base_t base;

	//On définit ensuite les attributs, mais on ne pourra pas y accéder directement avec micropython
	mp_uint_t temperature;

	const char* name;

} _example_Room_obj_t;

//Ajout d'une méthode à une class :
static mp_obj_t example_Room_getTemp(mp_obj_t self_in) {

	// Le premier argument est self, comme quand on définit une méthode d'une classe en python, mais ici on doit le cast
	_example_Room_obj_t *self = MP_OBJ_TO_PTR(self_in);

	//On récupère la température en oubliant pas de cast en int micropython
	mp_uint_t temp = self->temperature;
	return mp_obj_new_int_from_uint(temp);
}

// On oublie pas d'associer la fonction à une fonction python :
static MP_DEFINE_CONST_FUN_OBJ_1(example_Room_getTemp_obj, example_Room_getTemp);

// On change les fonctions Room.__new__ et Room.__init__ :
static mp_obj_t example_Room_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
	_example_Room_obj_t *self;
	
	//On vérifie qu'il y a bien le bon nombre d'arguments :
	mp_arg_check_num(n_args,n_kw,1,2,false);
	
	//Alloue la mémoire pour l'objet et définit son type :
	self = mp_obj_malloc(_example_Room_obj_t, type);

	//On définit la température de base à 25°
	self->temperature = 25;

	self->name = mp_obj_str_get_str(args[0]);

	// Cette fonction doit toujour retourner self :
	return MP_OBJ_FROM_PTR(self);
}

// Ajout des fonctions Room.__repr__ et Room.__str__ :
static void example_Room_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
	mp_printf(print, "%q()", MP_QSTR_Room);

	//To make difference betwteen __repr__ and __str__
	if (kind == PRINT_STR) {
		_example_Room_obj_t *self = MP_OBJ_TO_PTR(self_in);
		mp_printf(print, "  temperature : %d°", self->temperature);
	}
}

// Gère les attributs (Room.temperature par ex)
static void example_Room_attribute_handler(mp_obj_t self_in, qstr attr, mp_obj_t *dest) {

	// Il faut laisser passer les attributs qu'on a pas besoin de toucher, par exemple .getTemp()
	if (attr != MP_QSTR_temperature || attr != MP_QSTR_name) {
		dest[1] = MP_OBJ_SENTINEL;
		return;
	}

	_example_Room_obj_t *self = MP_OBJ_TO_PTR(self_in);
	
	//TODO: change this to catch access to variable, and if not let pass
	if (attr == MP_QSTR_temperature) {
		//On vérifie si c'est un read
		if (dest[0] == MP_OBJ_NULL) {
			//Dans ce cas, c'est un read, on stock la valeur dans dest[0] puis on fait un return
			dest[0] = mp_obj_new_int_from_uint(self->temperature);
			return;
		}
		
		//On vérifie si c'est un write ou un delete :
		else if (dest[0] == MP_OBJ_SENTINEL) {
			if (dest[1] == MP_OBJ_NULL) {
				//C'est un delete, mais ici, pas nécessaire de l'autoriser, donc on ne fait rien :
				return;
			} else {
				//C'est un write. On doit récupérer la valeur puis la stocker dans la variable
				mp_uint_t desired_temp = mp_obj_get_int(dest[1]);

				self->temperature = desired_temp;

				//On indique que l'opération de write est réussi :
				dest[0] = MP_OBJ_NULL;
				return;
			}
		}
	}
	return;
}

//Maintenant qu'on a toutes les méthodes nécessaires, on construit notre class pour micropython :
static const mp_rom_map_elem_t example_Room_locals_dict_table[] = {
	{ MP_ROM_QSTR(MP_QSTR_getTemp), MP_ROM_PTR(&example_Room_getTemp_obj)},
};
static MP_DEFINE_CONST_DICT(example_Room_locals_dict, example_Room_locals_dict_table);

// Définition de l'objet Room
MP_DEFINE_CONST_OBJ_TYPE(
	example_type_Room,
	MP_QSTR_Room,
	MP_TYPE_FLAG_NONE,
	attr, example_Room_attribute_handler,
	print, example_Room_print,
	make_new, example_Room_make_new,
	locals_dict, &example_Room_locals_dict
);

// On va mapper les noms des variables et des class :
static const mp_rom_map_elem_t example_module_globals_table[] = {
	{ MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_testlibrary)},
	{ MP_ROM_QSTR(MP_QSTR_multiply_ints), MP_ROM_PTR(&example_multiply_ints_obj)},
	{ MP_ROM_QSTR(MP_QSTR_Room), MP_ROM_PTR(&example_type_Room)},
};
static MP_DEFINE_CONST_DICT(example_module_globals, example_module_globals_table);

//On définit le module :
const mp_obj_module_t example_user_testlibrary = {
	.base = { &mp_type_module },
	.globals= (mp_obj_dict_t *)&example_module_globals
};

// Enregistrement le module pour le rendre accessible sous python :
MP_REGISTER_MODULE(MP_QSTR_testlibrary, example_user_testlibrary);
