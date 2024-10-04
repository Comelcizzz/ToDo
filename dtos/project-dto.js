// Data Transfer Object (DTO) for Project
// This class is used to define the structure of the project data 
// that will be transferred between the application layers (e.g., 
// from the database to the client). It ensures that only the 
// relevant data is exposed and can help in keeping a consistent 
// format.

module.exports = class ProjectDto {
    // Unique identifier for the project
    _id;
    // Title of the project
    title;
    // Description of the project
    description;

    // Constructor to initialize the ProjectDto with data from the model
    constructor(model) {
        // Set the ID, ensuring it's taken from the model's 'id' property
        this._id = model.id; 
        // Set the title from the model
        this.title = model.title;
        // Set the description from the model
        this.description = model.description;
    }
}
